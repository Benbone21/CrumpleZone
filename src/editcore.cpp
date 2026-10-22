#include "editcore.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>

#include "modelkit.h"
#include "objshape.h"
#include "platform.h"
#include "props.h"
#include "sectionfile.h"

// ---- Materials ---------------------------------------------------------------------------

const std::vector<PaintMaterial>& paintMaterials() {
    // Swatches are roughly what the terrain shader draws for each paint.
    static const std::vector<PaintMaterial> m = {
        {"GRASS", SURF_GRASS, PAINT_NONE, {0.32f, 0.46f, 0.2f}},
        {"DIRT TRAIL", SURF_DIRT, PAINT_TRAIL, {0.5f, 0.38f, 0.25f}},
        {"MUD", SURF_MUD, PAINT_MUD, {0.3f, 0.22f, 0.14f}},
        {"SAND", SURF_SAND, PAINT_SAND, {0.82f, 0.74f, 0.55f}},
        {"DESERT SAND", SURF_SAND, PAINT_DESERT, {0.8f, 0.6f, 0.4f}},
        {"GRAVEL", SURF_GRAVEL, PAINT_GRAVEL, {0.55f, 0.53f, 0.5f}},
        {"ROCK", SURF_ROCK, PAINT_ROCK, {0.5f, 0.49f, 0.47f}},
        {"RED ROCK", SURF_ROCK, PAINT_REDROCK, {0.66f, 0.36f, 0.24f}},
        {"SCREE", SURF_SCREE, PAINT_SCREE, {0.6f, 0.58f, 0.55f}},
        {"SNOW", SURF_SNOW, PAINT_SNOW, {0.93f, 0.95f, 0.98f}},
        {"ICE", SURF_ICE, PAINT_ICE, {0.7f, 0.85f, 0.95f}},
        {"CONCRETE", SURF_PAVED, PAINT_PAD, {0.68f, 0.67f, 0.64f}},
        {"TARMAC", SURF_PAVED, PAINT_TRACK, {0.22f, 0.22f, 0.23f}},
        {"WET TARMAC", SURF_WET, PAINT_WET, {0.15f, 0.16f, 0.18f}},
    };
    return m;
}

std::string levelFileName(const std::string& name) {
    std::string f;
    for (char ch : name) {
        if (std::isalnum((unsigned char)ch)) f += (char)std::tolower((unsigned char)ch);
        else if (!f.empty() && f.back() != '_') f += '_';
    }
    while (!f.empty() && f.back() == '_') f.pop_back();
    if (f.empty()) f = "level";
    if (f.size() > 40) f.resize(40);
    return f;
}

namespace {

const double kMinHeight = -60, kMaxHeight = 500;

// Closest approach of the ray o + t d (t >= 0, |d| = 1) to the segment a-b: the distance, and t.
double raySegment(const V3& o, const V3& d, const V3& a, const V3& b, double& t) {
    const V3 u = b - a, w = o - a;
    const double uu = dot(u, u), du = dot(d, u), dw = dot(d, w), uw = dot(u, w);
    const double den = uu - du * du;
    double s = uu < 1e-12 ? 0 : den > 1e-9 ? clampd((uw - du * dw) / den, 0, 1) : 0;
    t = std::max(0.0, dot(a + u * s - o, d));
    // (Re-project once both ways so clamping on either side settles.)
    if (uu > 1e-12) s = clampd(dot(o + d * t - a, u) / uu, 0, 1);
    t = std::max(0.0, dot(a + u * s - o, d));
    return len(o + d * t - (a + u * s));
}

bool raySphere(const V3& o, const V3& d, const V3& c, double r, double& t) {
    const V3 m = o - c;
    const double b = dot(m, d), cc = dot(m, m) - r * r;
    if (cc > 0 && b > 0) return false;
    const double disc = b * b - cc;
    if (disc < 0) return false;
    t = std::max(0.0, -b - std::sqrt(disc));
    return true;
}

// ---- Exact ray tests (what a click can land on is the object itself, not a sphere round it) ----

// The ray against an upright cone (axis through x0, z0; circular section R at y0 narrowing to nothing at y1). The distance from the axis
// less the section's radius is convex along the ray, so its dip below zero is found by ternary search.
bool rayCone(const V3& o, const V3& d, double x0, double z0, double y0, double y1, double R, double& t) {
    double ta = 0, tb = 1e4;
    if (std::fabs(d.y) > 1e-9) {
        double a = (y0 - o.y) / d.y, b = (y1 - o.y) / d.y;
        if (a > b) std::swap(a, b);
        ta = std::max(0.0, a), tb = std::min(tb, b);
    } else if (o.y < y0 || o.y > y1) {
        return false;
    }
    if (ta > tb) return false;
    auto f = [&](double tt) {
        const V3 q = o + d * tt;
        return std::hypot(q.x - x0, q.z - z0) - R * clampd((y1 - q.y) / (y1 - y0), 0, 1);
    };
    double lo = ta, hi = tb;
    for (int k = 0; k < 60; k++) {
        const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
        (f(m1) < f(m2) ? hi : lo) = (f(m1) < f(m2) ? m2 : m1);
    }
    const double tm = (lo + hi) / 2;
    if (f(tm) > 0) return false;
    if (f(ta) <= 0) {
        t = ta;
        return true;
    }
    lo = ta, hi = tm;
    for (int k = 0; k < 40; k++) {
        const double m = (lo + hi) / 2;
        (f(m) > 0 ? lo : hi) = m;
    }
    t = hi;
    return true;
}

// An oriented box standing on `at` (yaw about up, local frame as World::addBox's).
Box obb(const V3& c, const V3& half, double yaw) {
    Box b;
    b.c = c, b.h = half;
    b.ax[0] = rotateAxis(V3(1, 0, 0), V3(0, 1, 0), yaw), b.ax[1] = V3(0, 1, 0), b.ax[2] = rotateAxis(V3(0, 0, 1), V3(0, 1, 0), yaw);
    b.bound = len(half);
    return b;
}

// The box round a placed model's mesh.
Box modelBox(const Placed& pl) {
    const mk::Mesh& m = mk::modelMesh(pl.id);
    if (m.v.empty()) return obb(pl.p, V3(0.3, 0.3, 0.3), pl.yaw);
    const V3 lo(m.lo.x * pl.scale.x, m.lo.y * pl.scale.y, m.lo.z * pl.scale.z), hi(m.hi.x * pl.scale.x, m.hi.y * pl.scale.y, m.hi.z * pl.scale.z);
    const V3 mid = (lo + hi) * 0.5, half = (hi - lo) * 0.5;
    if (pl.oriented) {  // (tipped: its own frame)
        Box b = obb(pl.p + pl.ax[0] * mid.x + pl.ax[1] * mid.y + pl.ax[2] * mid.z, V3(std::max(0.05, half.x), std::max(0.05, half.y), std::max(0.05, half.z)), 0);
        for (int i = 0; i < 3; i++) b.ax[i] = pl.ax[i];
        return b;
    }
    const V3 cx = rotateAxis(V3(mid.x, 0, mid.z), V3(0, 1, 0), pl.yaw);
    return obb(pl.p + V3(cx.x, mid.y, cx.z), V3(std::max(0.05, half.x), std::max(0.05, half.y), std::max(0.05, half.z)), pl.yaw);
}

// The ray against an ellipsoid (centre c, horizontal radius rx, vertical ry).
bool rayEllipsoid(const V3& o, const V3& d, const V3& c, double rx, double ry, double& t) {
    const double k = rx / ry;
    const V3 q(o.x - c.x, (o.y - c.y) * k, o.z - c.z), e(d.x, d.y * k, d.z);
    const double a = dot(e, e), b = dot(q, e), cc = dot(q, q) - rx * rx;
    const double disc = b * b - a * cc;
    if (disc < 0 || a < 1e-12) return false;
    const double t0 = (-b - std::sqrt(disc)) / a, t1 = (-b + std::sqrt(disc)) / a;
    if (t1 < 0) return false;
    t = std::max(0.0, t0);
    return true;
}

// The model a map fixture is drawn as (scenery.cpp's choice), with the one setting it has (a sign's rating, a cairn's or stump's shape).
bool propAsModel(const Prop& p, int index, Placed& pl, int& sub) {
    const int seed = index + 1;
    pl = Placed();
    pl.p = p.a, pl.yaw = p.yaw, sub = 0;
    switch (p.kind) {
        case PROP_CABIN: pl.id = "cabin"; break;
        case PROP_TABLE: pl.id = "picnic_table"; break;
        case PROP_MAPBOARD: pl.id = "map_board"; break;
        case PROP_MARKER: pl.id = "marker_post"; break;
        case PROP_CAIRN: pl.id = "cairn", sub = 1 + seed % 9; break;
        case PROP_SIGN: pl.id = "trail_sign", sub = p.col[1] > 0.4 ? 0 : p.col[2] > 0.5 ? 1 : p.col[0] > 0.5 ? 3 : 2; break;
        case PROP_STUMP:
            pl.id = "stump", sub = 1 + seed % 6;
            pl.scale = V3(p.b.x / 0.35, p.b.y / 0.6, p.b.x / 0.35);
            break;
        default: return false;
    }
    return mk::findModel(pl.id) != nullptr;
}

inline bool isSpan(int kind) { return kind == OBJ_LOG || kind == OBJ_FENCE; }  // (an object that runs from p to p2)

// A fence: a thin band along it, upright.
Box fenceBox(const Prop& f) {
    const V3 d = f.b - f.a;
    return obb((f.a + f.b) * 0.5 + V3(0, 0.6, 0), V3(std::max(0.1, std::hypot(d.x, d.z) / 2), 0.7, 0.14), std::atan2(-d.z, d.x));
}

// A repair shed: its pad and the shed over it (the posts are at +-5 m along, +-3.7 m across).
Box shedBox(const Prop& f) { return obb(f.a + V3(0, 2.5, 0), V3(5.4, 2.5, 4.0), f.yaw); }

// A tree: its trunk, and its crown (a cone for a pine, a ball for a broadleaf; a dead snag is all trunk).
bool rayTree(const V3& o, const V3& d, const Tree& tr, double& tBest) {
    bool hit = false;
    double t;
    const double trunk = tr.kind == 1 ? 0.34 : 0.28;
    if (raySegment(o, d, tr.base, tr.base + V3(0, tr.height * (tr.kind == 2 ? 0.95 : 0.6), 0), t) < trunk) hit = true, tBest = t;
    if (tr.kind == 0 && rayCone(o, d, tr.base.x, tr.base.z, tr.base.y + tr.height * 0.18, tr.base.y + tr.height, tr.height * 0.17, t)) {
        if (!hit || t < tBest) tBest = t;
        hit = true;
    }
    if (tr.kind == 1 && raySphere(o, d, tr.base + V3(0, tr.height * 0.64, 0), tr.height * 0.28, t)) {
        if (!hit || t < tBest) tBest = t;
        hit = true;
    }
    return hit;
}

// Outlines.
void boxEdges(const Box& b, Shape& s, bool faces) {
    V3 c[8];
    for (int k = 0; k < 8; k++) c[k] = b.c + b.ax[0] * ((k & 1 ? 1 : -1) * b.h.x) + b.ax[1] * ((k & 2 ? 1 : -1) * b.h.y) + b.ax[2] * ((k & 4 ? 1 : -1) * b.h.z);
    for (int k = 0; k < 8; k++)
        for (int bit = 1; bit < 8; bit <<= 1)
            if (!(k & bit)) s.edges.push_back({c[k], c[k | bit]});
    if (!faces) return;
    // (Two triangles a face; the centre decides which way is out.)
    static const int quads[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    auto face = [&](const V3& a, const V3& b2, const V3& c2) {
        const V3 n = cross(b2 - a, c2 - a);
        if (dot(n, (a + b2 + c2) * (1.0 / 3) - b.c) < 0) s.tris.push_back({a, c2, b2});
        else s.tris.push_back({a, b2, c2});
    };
    for (const auto& q : quads) {
        face(c[q[0]], c[q[1]], c[q[2]]);
        face(c[q[0]], c[q[2]], c[q[3]]);
    }
}

void ringEdges(Shape& s, const V3& c, const V3& u, const V3& v, double r, int n = 14) {
    for (int k = 0; k < n; k++) {
        const double a0 = 2 * M_PI * k / n, a1 = 2 * M_PI * (k + 1) / n;
        s.edges.push_back({c + u * (std::cos(a0) * r) + v * (std::sin(a0) * r), c + u * (std::cos(a1) * r) + v * (std::sin(a1) * r)});
    }
}

bool sameObject(const LevelObject& a, const LevelObject& b) {
    auto eq = [](const V3& p, const V3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.kind == b.kind && eq(a.p, b.p) && eq(a.size, b.size) && eq(a.p2, b.p2) && a.yaw == b.yaw && a.pitch == b.pitch &&
           a.roll == b.roll && a.top == b.top && a.seed == b.seed && a.sub == b.sub && a.col[0] == b.col[0] && a.col[1] == b.col[1] &&
           a.col[2] == b.col[2] && a.name == b.name && a.lift == b.lift && a.lift2 == b.lift2 && a.rel == b.rel && a.scan == b.scan;
}

bool sameObjects(const Level& a, const Level& b) {
    if (a.objects.size() != b.objects.size() || a.removed.size() != b.removed.size()) return false;
    for (size_t k = 0; k < a.objects.size(); k++)
        if (!sameObject(a.objects[k], b.objects[k])) return false;
    for (size_t k = 0; k < a.removed.size(); k++)
        if (a.removed[k].x != b.removed[k].x || a.removed[k].z != b.removed[k].z || a.removed[k].y != b.removed[k].y) return false;
    return true;
}

}  // namespace

// ---- The brush mask -----------------------------------------------------------------------

const char* brushShapeName(int s) {
    static const char* n[SHAPE_COUNT] = {"ROUND", "SQUARE", "DIAMOND"};
    return n[std::min(std::max(s, 0), (int)SHAPE_COUNT - 1)];
}
const char* brushFallName(int f) {
    static const char* n[FALL_COUNT] = {"HARD", "SMOOTH", "LINEAR"};
    return n[std::min(std::max(f, 0), (int)FALL_COUNT - 1)];
}
const char* brushStyleName(int s) {
    static const char* n[STYLE_COUNT] = {"SOLID", "SPLATTER", "SPECKLE", "NOISE", "STRIPES"};
    return n[std::min(std::max(s, 0), (int)STYLE_COUNT - 1)];
}

double brushStep(double r, int dir) {
    static const double ladder[] = {0.5, 1, 1.5, 2, 3, 4, 6, 8, 11, 16, 22, 32, 45, 64, 80, 100};
    const int n = (int)(sizeof ladder / sizeof ladder[0]);
    int at = 0;
    for (int k = 1; k < n; k++)
        if (std::fabs(std::log(ladder[k] / r)) < std::fabs(std::log(ladder[at] / r))) at = k;
    return ladder[std::min(std::max(at + dir, 0), n - 1)];
}

namespace {

// A hash of a lattice point and a seed, 0..1.
double hash01(int ix, int iz, uint32_t seed) {
    uint32_t h = (uint32_t)ix * 374761393u + (uint32_t)iz * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    return (h & 0xffffff) / 16777216.0;
}

double valueNoise(double x, double z, uint32_t seed) {
    const double fx = std::floor(x), fz = std::floor(z);
    const int ix = (int)fx, iz = (int)fz;
    const double tx = x - fx, tz = z - fz, sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
    const double a = hash01(ix, iz, seed), b = hash01(ix + 1, iz, seed), c = hash01(ix, iz + 1, seed), d = hash01(ix + 1, iz + 1, seed);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
}

// How far (0 at the center, 1 at the rim) a point is from the center, in the brush's shape.
double shapeDist(BrushShape s, double dx, double dz, double r) {
    switch (s) {
        case SHAPE_SQUARE: return std::max(std::fabs(dx), std::fabs(dz)) / r;
        case SHAPE_DIAMOND: return (std::fabs(dx) + std::fabs(dz)) / r;
        default: return std::hypot(dx, dz) / r;
    }
}

double fallOf(BrushFall f, double q) {  // q: 0..1 from the center
    switch (f) {
        case FALL_HARD: return 1.0;
        case FALL_LINEAR: return 1.0 - q;
        default: return 0.5 * (1 + std::cos(M_PI * q));
    }
}

}  // namespace

double brushNoise(double x, double z, double scale, uint32_t seed) {
    return 0.65 * valueNoise(x / scale, z / scale, seed) + 0.35 * valueNoise(x / scale * 2.3 + 19.7, z / scale * 2.3 - 7.1, seed + 101);
}

double brushMask(const Brush& b, double cx, double cz, double x, double z) {
    const double R = std::max(b.radius, 0.5);
    if (R < 1.0) cx = std::round(cx), cz = std::round(cz);  // (one cell: the one the cursor is over)
    const double dx = x - cx, dz = z - cz;
    // Distance as a share of the rim; the falloff reaches zero half a cell past it so the rim cells still count.
    const double edge = shapeDist(b.shape, dx, dz, R);
    if (edge > 1.0 + 1e-9) return 0;
    const double q = R < 1.0 ? 0 : shapeDist(b.shape, dx, dz, R + 0.5);
    const double fall = fallOf(b.fall, std::min(q, 1.0));
    if (fall <= 0) return 0;
    const double dens = clampd(b.density, 0, 1);
    switch (b.style) {
        case STYLE_SOLID: return fall;
        case STYLE_SPECKLE: return hash01((int)std::lround(x), (int)std::lround(z), b.seed) < dens * (0.3 + 0.7 * fall) ? fall : 0;
        case STYLE_NOISE: {
            // Mottled blotches, a few meters across (bigger on bigger brushes).
            const double n = brushNoise(x, z, clampd(R * 0.3, 1.5, 9), b.seed);
            const double t = 0.5 + (0.5 - dens) * 0.42;
            return fall * smoothstep(t - 0.06, t + 0.06, n);
        }
        case STYLE_STRIPES: {
            const double period = clampd(R * 0.35, 2.0, 10.0);
            double u = (x * std::cos(b.angle) + z * std::sin(b.angle)) / period;
            u -= std::floor(u);
            if (u >= dens) return 0;
            const double soft = std::min(0.15, dens / 2);
            return fall * smoothstep(0, soft, u) * (1 - smoothstep(dens - soft, dens, u));
        }
        case STYLE_SPLATTER: {
            // Round dabs scattered over a jittered lattice; farther from the middle, fewer of them.
            const double dab = clampd(R * 0.2, 0.8, 8), sp = dab * 1.3;
            const int gx = (int)std::floor(x / sp), gz = (int)std::floor(z / sp);
            double best = 0;
            for (int a = -1; a <= 1; a++)
                for (int c = -1; c <= 1; c++) {
                    const int lx = gx + a, lz = gz + c;
                    const double px = (lx + 0.15 + 0.7 * hash01(lx, lz, b.seed)) * sp, pz = (lz + 0.15 + 0.7 * hash01(lx, lz, b.seed + 1)) * sp;
                    const double cq = std::min(1.0, shapeDist(b.shape, px - cx, pz - cz, R + 0.5));
                    if (hash01(lx, lz, b.seed + 2) >= dens * (0.35 + 0.65 * fallOf(b.fall == FALL_HARD ? FALL_LINEAR : b.fall, cq))) continue;
                    const double rad = dab * (0.55 + 0.5 * hash01(lx, lz, b.seed + 3)) * (0.7 + 0.5 * dens);
                    const double d = std::hypot(x - px, z - pz);
                    if (d < rad) best = std::max(best, 1 - smoothstep(0.65, 1.0, d / rad));
                }
            return best;
        }
        default: return fall;
    }
}

// ---- Starting --------------------------------------------------------------------------

void EditCore::applyCells(World& w) {
    // The cells, as applyLevel puts them (the objects come next, from the map's own).
    const int n = w.gridSize();
    for (const auto& h : level.heights)
        if (h.first >= 0 && h.first < n * n) w.setCellHeight(h.first, h.second);
    for (const auto& s : level.surfaces)
        if (s.first >= 0 && s.first < n * n) w.setCellSurface(s.first, (uint8_t)(s.second >> 8), (uint8_t)(s.second & 0xff));
    for (const auto& wt : level.water)
        if (wt.first >= 0 && wt.first < n * n) w.setCellWater(wt.first, wt.second);
    for (const auto& f : level.flow)
        if (f.first >= 0 && f.first < n * n && !w.flowX.empty()) w.flowX[f.first] = f.second.first, w.flowZ[f.first] = f.second.second;
}

void EditCore::begin(World& w, const Level& lv) {
    world = &w;
    base.rocks = w.rocks, base.trees = w.trees, base.poles = w.poles, base.logs = w.logs;
    base.boxes = w.boxes, base.fixtures = w.fixtures, base.stations = w.stations, base.models = w.models, base.bushes = w.bushes;
    base.spawn = w.spawn, base.spawnYaw = w.spawnYaw;
    level = lv;
    level.map = w.map;
    origin = level;
    applyCells(w);
    rebuildObjects();
    if (level.section.on) clearBushes();
    syncSpawn();
    history.clear();
    future.clear();
    pending = Entry();
    pendingCells.clear();
    depth = 0;
    changes = savedChanges = 0;
    autosavedChanges = -1;
    const int n = w.gridSize();
    dirtyCells.add(0, 0);
    dirtyCells.add(n - 1, n - 1);
    dirtyWater = dirtyObjects = true;
}

void EditCore::resume(World& w) {
    world = &w;
    applyCells(w);
    rebuildObjects();
    if (level.section.on) clearBushes();
    syncSpawn();
    const int n = w.gridSize();
    dirtyCells.add(0, 0);
    dirtyCells.add(n - 1, n - 1);
    dirtyWater = dirtyObjects = true;
}

EditCore::PlaySpot EditCore::playSpot(bool here, const V3& at, double yaw) const {
    PlaySpot s;
    if (here) {
        s.here = true, s.p = V3(at.x, world ? world->height(at.x, at.z) : at.y, at.z), s.yaw = yaw;
    } else if (level.hasStart) {
        s.fromStart = true, s.p = level.start, s.yaw = level.startYaw;
    } else if (world) {
        s.p = world->spawn, s.yaw = world->spawnYaw;
    }
    return s;
}

Level EditCore::levelToPlay(bool here) const {
    Level l = level;
    if (here) l.mode = LEVEL_FREE;  // (a run needs its grid and its start: from here it's the world, with the course in view)
    return l;
}

Level EditCore::discard(const std::string& dir) {
    Level back;
    if (level.file.empty() || level.builtin || !loadLevelFile(level.file, back)) back = origin;
    if (!dir.empty())
        for (const std::string* n : {&level.name, &origin.name, &back.name}) std::remove((dir + "/" + levelFileName(*n) + ".czl").c_str());
    autosavedChanges = -1;
    return back;
}

std::string EditCore::autosavePath(const std::string& dir) const { return dir + "/" + levelFileName(level.name) + ".czl"; }

bool EditCore::autosave(const std::string& dir, std::string* err) {
    if (dir.empty()) return false;
    const size_t slash = dir.find_last_of("/\\");
    if (slash != std::string::npos) makeDir(configDir()), makeDir(dir.substr(0, slash));  // (levels/, then levels/autosave/ in saveLevelFile)
    Level out = level;
    out.builtin = false;
    if (!saveLevelFile(out, autosavePath(dir), err, false)) return false;
    autosavedChanges = changes;
    return true;
}

void EditCore::rebuildObjects() {
    World& w = *world;
    w.rocks = base.rocks, w.trees = base.trees, w.poles = base.poles, w.logs = base.logs;
    w.boxes = base.boxes, w.fixtures = base.fixtures, w.stations = base.stations, w.models = base.models, w.bushes = base.bushes;
    if (level.section.on) clearBushes();  // (a section's canvas is cleared of them)
    for (const V3& r : level.removed) w.removeNear(r.x, r.z, 1.5, (int)std::lround(r.y) - 1);
    baseCount[0] = (int)w.rocks.size(), baseCount[1] = (int)w.trees.size(), baseCount[2] = (int)w.logs.size();
    baseCount[3] = (int)w.boxes.size(), baseCount[4] = (int)w.fixtures.size(), baseCount[5] = (int)w.models.size(), baseCount[6] = (int)w.bushes.size();
    for (int k = 0; k < 7; k++) owner[k].assign(baseCount[k], -1);
    for (int k = 0; k < (int)level.objects.size(); k++) {
        LevelObject& o = level.objects[k];
        // A box or log's height is an offset from the ground (an absolute one, from an older file or a generator, becomes that now).
        if (o.kind == OBJ_BOX || o.kind == OBJ_LOG) {
            if (o.rel) resolveObject(w, o);
            else settleObject(w, o);
        }
        const size_t sz[7] = {w.rocks.size(), w.trees.size(), w.logs.size(), w.boxes.size(), w.fixtures.size(), w.models.size(), w.bushes.size()};
        placeObject(o, w);
        const size_t now[7] = {w.rocks.size(), w.trees.size(), w.logs.size(), w.boxes.size(), w.fixtures.size(), w.models.size(), w.bushes.size()};
        for (int q = 0; q < 7; q++)
            for (size_t i = sz[q]; i < now[q]; i++) owner[q].push_back(k);
    }
    w.buildGrid();
    dirtyObjects = true;
}

int EditCore::ownerOf(int baseKind, int index) const {
    if (baseKind < 0 || baseKind >= 7 || index < 0 || index >= (int)owner[baseKind].size()) return -1;
    return owner[baseKind][index];
}

void EditCore::syncSpawn() {
    World& w = *world;
    if (level.hasStart) {
        w.spawn = V3(level.start.x, w.height(level.start.x, level.start.z), level.start.z);
        w.spawnYaw = level.startYaw;
    } else {
        w.spawn = base.spawn, w.spawnYaw = base.spawnYaw;
    }
}

// ---- Edit bookkeeping ------------------------------------------------------------------

void EditCore::beginEdit(const std::string& what, const std::string& mergeKey) {
    if (depth++ > 0) return;
    pending = Entry();
    pending.what = what;
    pending.mergeKey = mergeKey;
    pending.time = clock;
    pendingCells.clear();
}

void EditCore::endEdit() {
    if (depth <= 0 || --depth > 0) return;
    for (CellChange& c : pending.cells) {
        c.worldNew = worldCell(c.id, c.what);
        const std::map<int, float>* fm = c.what == 0 ? &level.heights : c.what == 2 ? &level.water : nullptr;
        if (fm) {
            auto it = fm->find(c.id);
            c.hasNew = it != fm->end();
            c.lvNew = c.hasNew ? it->second : 0;
        } else {
            auto it = level.surfaces.find(c.id);
            c.hasNew = it != level.surfaces.end();
            c.lvNew = c.hasNew ? it->second : 0;
        }
    }
    if (pending.meta) pending.after = metaOf();
    const bool empty = pending.cells.empty() && !pending.meta;
    if (empty) return;
    // Rocks and trees stand on the ground: if the ground moved under one of the level's
    // objects, put it back on top (as loading the level would).
    if (!pending.cells.empty() && !level.objects.empty()) {
        CellRegion r;
        for (const CellChange& c : pending.cells)
            if (c.what == 0) r.add(c.id / world->gridSize(), c.id % world->gridSize());
        bool hit = false;
        const double half = world->halfSize();
        for (const LevelObject& o : level.objects) {
            // (a log rests on the ground at its two ends; a box on its footprint)
            const double m = o.kind == OBJ_LOG ? 3.0 : std::max(3.0, std::max(o.size.x, o.size.z) + 2);
            for (const V3* e : {&o.p, &o.p2}) {
                if (e == &o.p2 && o.kind != OBJ_LOG) break;
                const double i = e->x + half, j = e->z + half;
                if (!r.empty() && i + m >= r.i0 && i - m <= r.i1 && j + m >= r.j0 && j - m <= r.j1) hit = true;
            }
        }
        if (hit) rebuildObjects();
    }
    future.clear();
    changes++;
    // Quick repeats of one setting (a slider dragged, a value clicked along) are one step.
    if (!pending.mergeKey.empty() && !history.empty() && history.back().mergeKey == pending.mergeKey && pending.cells.empty() &&
        history.back().cells.empty() && pending.time - history.back().time < 1.5) {
        history.back().after = pending.after;
        history.back().time = pending.time;
    } else {
        history.push_back(std::move(pending));
        trim();
    }
    pending = Entry();
    pendingCells.clear();
}

void EditCore::trim() {
    // A bounded history: the last 120 steps, and no more than ~3 million cell changes.
    size_t cells = 0;
    for (const Entry& e : history) cells += e.cells.size();
    while (history.size() > 1 && (history.size() > 120 || cells > 3000000)) {
        cells -= history.front().cells.size();
        history.erase(history.begin());
    }
}

Level EditCore::metaOf() {
    std::map<int, float> h, wt;
    std::map<int, uint16_t> s;
    std::map<int, std::pair<float, float>> fl;
    h.swap(level.heights), s.swap(level.surfaces), wt.swap(level.water), fl.swap(level.flow);
    Level m = level;
    h.swap(level.heights), s.swap(level.surfaces), wt.swap(level.water), fl.swap(level.flow);
    return m;
}

void EditCore::restoreMeta(const Level& m) {
    const bool objs = !sameObjects(m, level);
    Level n = m;
    n.heights.swap(level.heights), n.surfaces.swap(level.surfaces), n.water.swap(level.water), n.flow.swap(level.flow);
    n.file = level.file;
    n.builtin = level.builtin;
    level = std::move(n);
    if (objs) rebuildObjects();
    syncSpawn();
}

void EditCore::touchMeta() {
    if (depth <= 0 || pending.meta) return;
    pending.meta = true;
    pending.before = metaOf();
}

double EditCore::worldCell(int id, int what) const {
    if (what == 0) return world->cellHeight(id);
    if (what == 1) return (double)(world->cellSurface(id) << 8 | world->cellPaint(id));
    const V3 c = world->cellCenter(id);
    return world->waterY.empty() ? world->waterLevel(c.x, c.z) : world->waterY[id];
}

void EditCore::markCell(int id) {
    const int n = world->gridSize();
    dirtyCells.add(id / n, id % n);
}

void EditCore::setCell(int id, int what, double v, bool inLevel, double lv) {
    if (what == 0) {
        world->setCellHeight(id, (float)v);
        if (inLevel) level.heights[id] = (float)lv;
        else level.heights.erase(id);
    } else if (what == 1) {
        const int sp = (int)v;
        world->setCellSurface(id, (uint8_t)(sp >> 8), (uint8_t)(sp & 0xff));
        if (inLevel) level.surfaces[id] = (uint16_t)lv;
        else level.surfaces.erase(id);
    } else {
        world->setCellWater(id, (float)v);
        if (inLevel) level.water[id] = (float)lv;
        else level.water.erase(id);
        dirtyWater = true;
    }
    markCell(id);
}

// The first time a cell changes in an edit, remember what it was.
#define CZ_REMEMBER(ID, WHAT, MAP)                                                                  \
    do {                                                                                             \
        const int key_ = (ID) * 4 + (WHAT);                                                          \
        if (!pendingCells.count(key_)) {                                                             \
            auto it_ = (MAP).find(ID);                                                               \
            CellChange c_{(ID), (uint8_t)(WHAT), it_ != (MAP).end(), false, worldCell((ID), (WHAT)), 0, \
                          it_ != (MAP).end() ? (double)it_->second : 0.0, 0};                         \
            pendingCells[key_] = pending.cells.size();                                               \
            pending.cells.push_back(c_);                                                             \
        }                                                                                            \
    } while (0)

void EditCore::touchHeight(int id, float h) {
    h = (float)clampd(h, kMinHeight, kMaxHeight);
    CZ_REMEMBER(id, 0, level.heights);
    world->setCellHeight(id, h);
    level.heights[id] = h;
    markCell(id);
}

void EditCore::touchSurface(int id, uint16_t sp) {
    CZ_REMEMBER(id, 1, level.surfaces);
    world->setCellSurface(id, (uint8_t)(sp >> 8), (uint8_t)(sp & 0xff));
    level.surfaces[id] = sp;
    markCell(id);
}

void EditCore::touchWater(int id, float lvl) {
    CZ_REMEMBER(id, 2, level.water);
    world->setCellWater(id, lvl);
    level.water[id] = lvl;
    markCell(id);
    dirtyWater = true;
}

#undef CZ_REMEMBER

// ---- Terrain ---------------------------------------------------------------------------

void EditCore::brush(BrushOp op, double cx, double cz, double r, double amount, double target) {
    Brush b;
    b.radius = r;
    brush(op, b, cx, cz, amount, target);
}

void EditCore::brush(BrushOp op, const Brush& b, double cx, double cz, double amount, double target) {
    if (!world) return;
    static const char* names[BRUSH_OP_COUNT] = {"RAISE", "LOWER", "SMOOTH", "FLATTEN", "ROUGHEN", "TERRACE"};
    const bool own = !editing();
    if (own) beginEdit(names[op]);
    const int n = world->gridSize();
    const double half = world->halfSize(), r = std::max(b.radius, 0.5);
    if (r < 1.0) cx = std::round(cx), cz = std::round(cz);
    const int i0 = std::max(1, (int)std::floor(cx + half - r)), i1 = std::min(n - 2, (int)std::ceil(cx + half + r));
    const int j0 = std::max(1, (int)std::floor(cz + half - r)), j1 = std::min(n - 2, (int)std::ceil(cz + half + r));
    if (i0 > i1 || j0 > j1) {
        if (own) endEdit();
        return;
    }
    // Smoothing reads the heights before this pass (a box blur a few cells wide).
    std::vector<float> blurred;
    const int k = std::max(1, std::min(6, (int)std::lround(r / 6)));
    const int Hh = j1 - j0 + 1;
    if (op == BRUSH_SMOOTH) {
        const int W = i1 - i0 + 1;
        blurred.assign((size_t)W * Hh, 0);
        for (int i = i0; i <= i1; i++)
            for (int j = j0; j <= j1; j++) {
                if (brushMask(b, cx, cz, i - half, j - half) <= 0) continue;
                double s = 0;
                int c = 0;
                for (int a = -k; a <= k; a++)
                    for (int bb = -k; bb <= k; bb++) {
                        const int ii = std::min(std::max(i + a, 0), n - 1), jj = std::min(std::max(j + bb, 0), n - 1);
                        s += world->cellHeight(ii * n + jj);
                        c++;
                    }
                blurred[(size_t)(i - i0) * Hh + (j - j0)] = (float)(s / c);
            }
    }
    const double step = std::max(0.05, target);
    const double bump = clampd(r * 0.12, 1.5, 6.0);  // roughen: how wide the bumps are
    for (int i = i0; i <= i1; i++)
        for (int j = j0; j <= j1; j++) {
            const double x = i - half, z = j - half, f = brushMask(b, cx, cz, x, z);
            if (f <= 0) continue;
            const int id = i * n + j;
            const double h = world->cellHeight(id);
            double nh = h;
            switch (op) {
                case BRUSH_RAISE: nh = h + amount * f; break;
                case BRUSH_LOWER: nh = h - amount * f; break;
                case BRUSH_FLATTEN: nh = h + (target - h) * std::min(1.0, amount * f); break;
                case BRUSH_SMOOTH: nh = h + (blurred[(size_t)(i - i0) * Hh + (j - j0)] - h) * std::min(1.0, amount * f); break;
                case BRUSH_ROUGHEN: nh = h + amount * f * (brushNoise(x, z, bump, 77 + b.seed) * 2 - 1); break;
                case BRUSH_TERRACE: nh = h + (std::round(h / step) * step - h) * std::min(1.0, amount * f); break;
                default: break;
            }
            if (std::fabs(nh - h) > 1e-5) touchHeight(id, (float)nh);
        }
    if (own) endEdit();
}

void EditCore::ramp(const V3& a, const V3& b, double width) {
    if (!world) return;
    const bool own = !editing();
    if (own) beginEdit("RAMP");
    const int n = world->gridSize();
    const double half = world->halfSize(), hw = width / 2, shoulder = std::max(2.0, width * 0.5);
    const V3 a2(a.x, 0, a.z), u(b.x - a.x, 0, b.z - a.z);
    const double L2 = dot(u, u), reach = hw + shoulder;
    const int i0 = std::max(0, (int)std::floor(std::min(a.x, b.x) + half - reach)), i1 = std::min(n - 1, (int)std::ceil(std::max(a.x, b.x) + half + reach));
    const int j0 = std::max(0, (int)std::floor(std::min(a.z, b.z) + half - reach)), j1 = std::min(n - 1, (int)std::ceil(std::max(a.z, b.z) + half + reach));
    for (int i = i0; i <= i1; i++)
        for (int j = j0; j <= j1; j++) {
            const V3 p(i - half, 0, j - half);
            const double t = L2 > 1e-9 ? clampd(dot(p - a2, u) / L2, 0, 1) : 0;
            const double d = len(p - (a2 + u * t));
            if (d >= reach) continue;
            const double w = d <= hw ? 1.0 : 1 - smoothstep(0, 1, (d - hw) / shoulder);
            const int id = i * n + j;
            const double h = world->cellHeight(id), target = a.y + (b.y - a.y) * t;
            const double nh = h + (target - h) * w;
            if (std::fabs(nh - h) > 1e-5) touchHeight(id, (float)nh);
        }
    if (own) endEdit();
}

void EditCore::levelPad(const V3& c, double yaw, double hl, double hw, double y, double margin) {
    if (!world) return;
    const bool own = !editing();
    if (own) beginEdit("LEVEL GROUND");
    const int n = world->gridSize();
    const double half = world->halfSize(), R = std::hypot(hl, hw) + margin;
    const V3 f(std::cos(yaw), 0, -std::sin(yaw)), s(-f.z, 0, f.x);
    for (int i = std::max(0, (int)(c.x + half - R)); i <= std::min(n - 1, (int)(c.x + half + R + 1)); i++)
        for (int j = std::max(0, (int)(c.z + half - R)); j <= std::min(n - 1, (int)(c.z + half + R + 1)); j++) {
            const V3 p(i - half - c.x, 0, j - half - c.z);
            const double ex = std::max(0.0, std::fabs(dot(p, f)) - hl), ez = std::max(0.0, std::fabs(dot(p, s)) - hw), d = std::hypot(ex, ez);
            if (d >= margin) continue;
            const double w = 1 - smoothstep(0, 1, d / margin);
            const int id = i * n + j;
            const double h = world->cellHeight(id), nh = h + (y - h) * w;
            if (std::fabs(nh - h) > 1e-5) touchHeight(id, (float)nh);
        }
    if (own) endEdit();
}

void EditCore::paint(double cx, double cz, double r, int material, bool soft) {
    Brush b;
    b.radius = r;
    b.fall = soft ? FALL_SMOOTH : FALL_HARD;
    paint(b, cx, cz, material);
}

void EditCore::paint(const Brush& b, double cx, double cz, int material) {
    if (!world) return;
    const std::vector<PaintMaterial>& mats = paintMaterials();
    const PaintMaterial& m = mats[std::min(std::max(material, 0), (int)mats.size() - 1)];
    const uint16_t sp = (uint16_t)(m.surface << 8 | m.paint);
    const bool own = !editing();
    if (own) beginEdit(std::string("PAINT ") + m.name);
    const int n = world->gridSize();
    const double half = world->halfSize(), r = std::max(b.radius, 0.5);
    if (r < 1.0) cx = std::round(cx), cz = std::round(cz);
    for (int i = std::max(0, (int)std::floor(cx + half - r)); i <= std::min(n - 1, (int)std::ceil(cx + half + r)); i++)
        for (int j = std::max(0, (int)std::floor(cz + half - r)); j <= std::min(n - 1, (int)std::ceil(cz + half + r)); j++) {
            const double f = brushMask(b, cx, cz, i - half, j - half);
            if (f <= 0) continue;
            const int id = i * n + j;
            // Dithered coverage: the full-strength core is solid, the rest speckles in with the brush's mask and opacity.
            const double cover = clampd(b.strength, 0, 1) * std::min(1.0, f * 2);
            if (cover < 1.0 && hash01(i, j, b.seed * 31 + 7) >= cover) continue;
            if ((uint16_t)(world->cellSurface(id) << 8 | world->cellPaint(id)) != sp || !level.surfaces.count(id)) touchSurface(id, sp);
        }
    if (own) endEdit();
}

int EditCore::flood(double cx, double cz, double depth, double maxR, bool* spilled) {
    if (spilled) *spilled = false;
    if (!world) return 0;
    const int n = world->gridSize();
    const int c0 = world->cellIndex(cx, cz);
    if (c0 < 0) return 0;
    auto waterAt = [&](int id) {
        const V3 c = world->cellCenter(id);
        return world->waterY.empty() ? world->waterLevel(c.x, c.z) : (double)world->waterY[id];
    };
    const double ground = world->cellHeight(c0), there = waterAt(c0);
    const double L = (there > ground ? there : ground) + depth;
    const bool own = !editing();
    if (own) beginEdit("FLOOD");
    std::vector<uint8_t> seen((size_t)n * n, 0);
    std::deque<int> q;
    q.push_back(c0);
    seen[c0] = 1;
    int count = 0;
    const int i0 = c0 / n, j0 = c0 % n;
    while (!q.empty() && count < 600000) {
        const int id = q.front();
        q.pop_front();
        if (world->cellHeight(id) >= L) continue;
        if (waterAt(id) < L - 1e-4) touchWater(id, (float)L);
        count++;
        const int i = id / n, j = id % n;
        const int nb[4][2] = {{i + 1, j}, {i - 1, j}, {i, j + 1}, {i, j - 1}};
        for (const auto& c : nb) {
            if (c[0] < 0 || c[1] < 0 || c[0] >= n || c[1] >= n) continue;
            const int nid = c[0] * n + c[1];
            if (seen[nid]) continue;
            seen[nid] = 1;
            if (std::hypot(c[0] - i0, c[1] - j0) > maxR) {
                if (world->cellHeight(nid) < L && spilled) *spilled = true;
                continue;
            }
            q.push_back(nid);
        }
    }
    if (own) endEdit();
    return count;
}

void EditCore::dry(double cx, double cz, double r) {
    if (!world || !world->hasWater()) return;
    const bool own = !editing();
    if (own) beginEdit("DRY");
    const int n = world->gridSize();
    const double half = world->halfSize();
    for (int i = std::max(0, (int)std::floor(cx + half - r)); i <= std::min(n - 1, (int)std::ceil(cx + half + r)); i++)
        for (int j = std::max(0, (int)std::floor(cz + half - r)); j <= std::min(n - 1, (int)std::ceil(cz + half + r)); j++) {
            if (std::hypot(i - half - cx, j - half - cz) > r) continue;
            const int id = i * n + j;
            if (worldCell(id, 2) > -1e8) touchWater(id, -1e9f);
        }
    if (own) endEdit();
}

int EditCore::dryBrush(const Brush& b, double cx, double cz) {
    if (!world || !world->hasWater()) return 0;
    const bool own = !editing();
    if (own) beginEdit("DRAIN");
    const int n = world->gridSize();
    const double half = world->halfSize(), r = std::max(b.radius, 0.5);
    if (r < 1.0) cx = std::round(cx), cz = std::round(cz);
    int count = 0;
    for (int i = std::max(0, (int)std::floor(cx + half - r)); i <= std::min(n - 1, (int)std::ceil(cx + half + r)); i++)
        for (int j = std::max(0, (int)std::floor(cz + half - r)); j <= std::min(n - 1, (int)std::ceil(cz + half + r)); j++) {
            const int id = i * n + j;
            if (worldCell(id, 2) <= -1e8 || brushMask(b, cx, cz, i - half, j - half) < 0.5) continue;
            touchWater(id, -1e9f);
            count++;
        }
    if (own) endEdit();
    return count;
}

int EditCore::drain(double cx, double cz) {
    if (!world || !world->hasWater()) return 0;
    const int n = world->gridSize();
    const double half = world->halfSize();
    // The wet cell nearest the click, within 3 m.
    int start = -1;
    double best = 1e9;
    for (int i = std::max(0, (int)std::lround(cx + half) - 3); i <= std::min(n - 1, (int)std::lround(cx + half) + 3); i++)
        for (int j = std::max(0, (int)std::lround(cz + half) - 3); j <= std::min(n - 1, (int)std::lround(cz + half) + 3); j++) {
            const double d = std::hypot(i - half - cx, j - half - cz);
            if (d < best && d <= 3.5 && worldCell(i * n + j, 2) > -1e8) best = d, start = i * n + j;
        }
    if (start < 0) return 0;
    const bool own = !editing();
    if (own) beginEdit("DRAIN POOL");
    std::vector<uint8_t> seen((size_t)n * n, 0);
    std::deque<int> q;
    q.push_back(start);
    seen[start] = 1;
    int count = 0;
    while (!q.empty() && count < 800000) {
        const int id = q.front();
        q.pop_front();
        touchWater(id, -1e9f);
        count++;
        const int i = id / n, j = id % n;
        for (int a = -1; a <= 1; a++)
            for (int c = -1; c <= 1; c++) {
                const int ni = i + a, nj = j + c;
                if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                const int nid = ni * n + nj;
                if (seen[nid]) continue;
                seen[nid] = 1;
                if (worldCell(nid, 2) > -1e8) q.push_back(nid);
            }
    }
    if (own) endEdit();
    return count;
}

// ---- Objects ---------------------------------------------------------------------------

LevelObject EditCore::grounded(LevelObject o) const {
    switch (o.kind) {
        case OBJ_BOX:
        case OBJ_LOG: resolveObject(*world, o); break;  // (boxes sit on the ground plus their lift, logs lie on it plus theirs)
        default: o.p.y = world->height(o.p.x, o.p.z); break;
    }
    return o;
}

static const char* kObjectNames[OBJ_KIND_COUNT] = {"ROCK", "BLOCK", "LOG", "TREE", "REPAIR SHOP", "STRUCTURE", "FENCE", "BUSH"};

void EditCore::addObjects(const std::vector<LevelObject>& objs, const std::string& what) {
    if (!world || objs.empty()) return;
    beginEdit(what);
    touchMeta();
    level.objects.insert(level.objects.end(), objs.begin(), objs.end());
    rebuildObjects();
    endEdit();
}

int EditCore::addObject(const LevelObject& o) {
    if (!world) return -1;
    beginEdit(std::string("ADD ") + kObjectNames[std::min(std::max(o.kind, 0), OBJ_KIND_COUNT - 1)]);
    touchMeta();
    level.objects.push_back(o);
    rebuildObjects();
    endEdit();
    return (int)level.objects.size() - 1;
}

void EditCore::setObject(int k, const LevelObject& o, const std::string& mergeKey) {
    if (!world || k < 0 || k >= (int)level.objects.size()) return;
    beginEdit(std::string("CHANGE ") + kObjectNames[std::min(std::max(o.kind, 0), OBJ_KIND_COUNT - 1)], mergeKey);
    touchMeta();
    level.objects[k] = o;
    rebuildObjects();
    endEdit();
}

void EditCore::removeObject(int k) {
    if (!world || k < 0 || k >= (int)level.objects.size()) return;
    beginEdit(std::string("DELETE ") + kObjectNames[std::min(std::max(level.objects[k].kind, 0), OBJ_KIND_COUNT - 1)]);
    touchMeta();
    level.objects.erase(level.objects.begin() + k);
    rebuildObjects();
    endEdit();
}

bool EditCore::removeBase(const EditPick& p) {
    if (!world || p.kind != EditPick::BASE) return false;
    beginEdit("DELETE MAP " + p.name);
    touchMeta();
    level.removed.push_back(V3(p.at.x, p.baseKind + 1, p.at.z));
    const int before = baseCount[p.baseKind];
    rebuildObjects();
    const bool gone = baseCount[p.baseKind] < before;
    endEdit();
    return gone;
}

int EditCore::adoptBase(const EditPick& p) {
    if (!world || p.kind != EditPick::BASE || p.baseKind != 4 || p.index < 0 || p.index >= (int)world->fixtures.size()) return -1;
    const Prop f = world->fixtures[p.index];
    if (f.kind != PROP_GARAGE) return -1;
    LevelObject o;
    o.kind = OBJ_STATION;
    o.p = V3(f.a.x, world->height(f.a.x, f.a.z), f.a.z);
    o.yaw = f.yaw;
    for (const Station& st : world->stations)
        if (std::hypot(st.p.x - f.a.x, st.p.z - f.a.z) < 0.05) o.name = st.name;
    beginEdit("MOVE MAP " + p.name);
    touchMeta();
    level.removed.push_back(V3(f.a.x, 4 + 1, f.a.z));
    level.objects.push_back(o);
    rebuildObjects();
    endEdit();
    return (int)level.objects.size() - 1;
}

// ---- Making the map's own objects the level's ---------------------------------------------------

bool EditCore::rockToObject(const Rock& r, LevelObject& o) const {
    const World& w = *world;
    o = LevelObject();
    o.kind = OBJ_ROCK, o.sub = 1, o.seed = r.seed, o.col[0] = r.tint;
    const props::HullLibrary* lib = props::activeRockScans();
    if (r.scan >= 0 && lib && r.scan < (int)lib->hulls.size()) {
        const props::RockHull& h = lib->hulls[r.scan];
        const V3 half = h.half(), mid((h.bmin[0] + h.bmax[0]) * 0.5, (h.bmin[1] + h.bmax[1]) * 0.5, (h.bmin[2] + h.bmax[2]) * 0.5);
        const double s0 = len(r.axis[0]), s1 = len(r.axis[1]), s2 = len(r.axis[2]);
        if (s0 < 1e-9 || s1 < 1e-9 || s2 < 1e-9) return false;
        const V3 f0 = r.axis[0] * (1 / s0), f1 = r.axis[1] * (1 / s1);
        o.pitch = std::asin(clampd(f0.y, -1, 1));
        o.yaw = std::atan2(-f0.z, f0.x);
        const double cy = std::cos(o.yaw), sy = std::sin(o.yaw), cp = std::cos(o.pitch), sp = std::sin(o.pitch);
        const V3 up0(-cy * sp, cp, sy * sp);
        o.roll = std::atan2(dot(cross(up0, f1), f0), dot(up0, f1));
        o.size = V3(s0 * half.x, s1 * half.y, s2 * half.z);
        o.scan = r.scan;
        const V3 at = r.origin + r.axis[0] * mid.x + r.axis[1] * mid.y + r.axis[2] * mid.z;
        double topY = -1e9;
        for (const V3& p : h.v) topY = std::max(topY, r.axis[0].y * (p.x - mid.x) + r.axis[1].y * (p.y - mid.y) + r.axis[2].y * (p.z - mid.z));
        o.p = V3(at.x, 0, at.z);
        o.top = at.y - w.height(at.x, at.z) + topY;
        return true;
    }
    // A code-made rock: a level rock whose box is the same as this one's (its lumps are the seed's, not these). The size is fitted:
    // build it, see how wide it comes out, and correct, so that pinning a rock never changes how big it looks.
    if (r.v.empty()) return false;
    V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
    for (const V3& p : r.v) lo = V3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)), hi = V3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    const V3 c = (lo + hi) * 0.5, want = (hi - lo) * 0.5;
    o.size = want;
    o.p = V3(c.x, 0, c.z);
    o.top = hi.y - w.height(c.x, c.z);
    World& tw = const_cast<World&>(w);  // (builds a rock and takes it away again)
    for (int it = 0; it < 4; it++) {
        const size_t n = tw.rocks.size();
        placeObject(o, tw);
        V3 l2(1e9, 1e9, 1e9), h2(-1e9, -1e9, -1e9);
        for (const V3& p : tw.rocks.back().v) l2 = V3(std::min(l2.x, p.x), std::min(l2.y, p.y), std::min(l2.z, p.z)), h2 = V3(std::max(h2.x, p.x), std::max(h2.y, p.y), std::max(h2.z, p.z));
        tw.rocks.resize(n);
        const V3 got = (h2 - l2) * 0.5;
        o.size = V3(o.size.x * want.x / std::max(got.x, 1e-3), o.size.y * want.y / std::max(got.y, 1e-3), o.size.z * want.z / std::max(got.z, 1e-3));
        o.top += hi.y - h2.y;
        // (the rock's middle may sit off the box's: slide it so the boxes meet)
        o.p.x += c.x - (l2.x + h2.x) * 0.5, o.p.z += c.z - (l2.z + h2.z) * 0.5;
    }
    return true;
}

// Rebuilds a box's axes from yaw, pitch, roll (as World::addBox does), for checking a conversion.
static void boxAxes(double yaw, double pitch, double roll, V3 ax[3]) {
    V3 a[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
    for (V3& v : a) {
        v = rotateAxis(v, V3(1, 0, 0), roll);
        v = rotateAxis(v, V3(0, 0, 1), pitch);
        v = rotateAxis(v, V3(0, 1, 0), yaw);
    }
    for (int i = 0; i < 3; i++) ax[i] = a[i];
}

int EditCore::resolveBase(const EditPick& p) const {
    const World& w = *world;
    auto near2 = [&](const V3& a) { return std::hypot(a.x - p.at.x, a.z - p.at.z); };
    int best = -1;
    double bd = 1.0;
    auto test = [&](int i, const V3& a) {
        const double d = near2(a);
        if (d < bd) bd = d, best = i;
    };
    switch (p.baseKind) {
        case 0: for (int i = 0; i < (int)w.rocks.size(); i++) test(i, w.rocks[i].c); break;
        case 1: for (int i = 0; i < (int)w.trees.size(); i++) test(i, w.trees[i].base); break;
        case 2: for (int i = 0; i < (int)w.logs.size(); i++) test(i, (w.logs[i].a + w.logs[i].b) * 0.5); break;
        case 3: for (int i = 0; i < (int)w.boxes.size(); i++) if (!w.boxes[i].hidden) test(i, w.boxes[i].c); break;
        case 4: for (int i = 0; i < (int)w.fixtures.size(); i++) test(i, w.fixtures[i].a); break;
        case 6: for (int i = 0; i < (int)w.bushes.size(); i++) test(i, w.bushes[i].p); break;
        default: break;
    }
    return best;
}

bool EditCore::baseToObject(const EditPick& p, LevelObject& o, V3& key) const {
    const World& w = *world;
    const int i = resolveBase(p);
    if (i < 0) return false;
    o = LevelObject();
    switch (p.baseKind) {
        case 0:
            if (!rockToObject(w.rocks[i], o)) return false;
            key = V3(w.rocks[i].c.x, 1, w.rocks[i].c.z);
            return true;
        case 1: {
            const Tree& t = w.trees[i];
            o.kind = OBJ_TREE, o.p = V3(t.base.x, 0, t.base.z), o.size = V3(0, t.height, 0), o.sub = t.kind, o.col[0] = t.tint;
            o.lift = t.base.y - (w.height(t.base.x, t.base.z) - 0.2);
            key = V3(t.base.x, 2, t.base.z);
            return true;
        }
        case 2: {
            const Log& g = w.logs[i];
            o.kind = OBJ_LOG, o.p = g.a, o.p2 = g.b, o.size = V3(g.r, 0, 0);
            settleObject(w, o);
            const V3 m = (g.a + g.b) * 0.5;
            key = V3(m.x, 3, m.z);
            return true;
        }
        case 3: {
            const Box& b = w.boxes[i];
            if (!b.solid) return false;
            o.kind = OBJ_BOX, o.p = b.c, o.size = b.h, o.sub = b.rock ? 1 : 0;
            o.col[0] = b.col[0], o.col[1] = b.col[1], o.col[2] = b.col[2];
            o.pitch = std::asin(clampd(b.ax[0].y, -1, 1));
            o.yaw = std::atan2(-b.ax[0].z, b.ax[0].x);
            V3 a1 = rotateAxis(rotateAxis(V3(0, 1, 0), V3(0, 0, 1), o.pitch), V3(0, 1, 0), o.yaw);
            o.roll = std::atan2(dot(cross(a1, b.ax[1]), b.ax[0]), dot(a1, b.ax[1]));
            V3 chk[3];
            boxAxes(o.yaw, o.pitch, o.roll, chk);
            for (int a = 0; a < 3; a++)
                if (len(chk[a] - b.ax[a]) > 1e-3) return false;  // (a tilt this can't say)
            settleObject(w, o);
            key = V3(b.c.x, 4, b.c.z);
            return true;
        }
        case 4: {
            const Prop& f = w.fixtures[i];
            key = V3(f.a.x, 5, f.a.z);
            if (f.kind == PROP_GARAGE) {
                o.kind = OBJ_STATION, o.p = V3(f.a.x, w.height(f.a.x, f.a.z), f.a.z), o.yaw = f.yaw;
                for (const Station& st : w.stations)
                    if (std::hypot(st.p.x - f.a.x, st.p.z - f.a.z) < 0.05) o.name = st.name;
                return true;
            }
            if (f.kind == PROP_FENCE) {
                o.kind = OBJ_FENCE, o.p = f.a, o.p2 = f.b;
                return true;
            }
            Placed pl;
            int sub;
            if (!propAsModel(f, i, pl, sub)) return false;
            o.kind = OBJ_MODEL, o.name = pl.id, o.sub = sub, o.yaw = f.yaw, o.size = pl.scale, o.p = V3(f.a.x, 0, f.a.z);
            o.lift = f.a.y - w.height(f.a.x, f.a.z);
            return true;
        }
        case 6: {
            const Bush& b = w.bushes[i];
            o.kind = OBJ_BUSH, o.p = V3(b.p.x, 0, b.p.z), o.size = V3(b.r, 0, 0), o.col[0] = b.tint, o.sub = plantKindOf(b.kind);
            o.lift = b.p.y - w.height(b.p.x, b.p.z);
            key = V3(b.p.x, 7, b.p.z);
            return true;
        }
        default: return false;
    }
}

bool EditCore::canAdopt(const EditPick& p) const {
    if (!world || p.kind != EditPick::BASE) return false;
    LevelObject o;
    V3 key;
    return baseToObject(p, o, key);
}

std::vector<int> EditCore::adopt(const std::vector<EditPick>& picks, const std::function<bool(const LevelObject&)>& wants, std::vector<char>* left) {
    std::vector<int> out(picks.size(), -1);
    if (left) left->assign(picks.size(), 0);
    if (!world) return out;
    // (Read them all before changing anything: the world's indices move once one is taken out.)
    std::vector<std::pair<LevelObject, V3>> made(picks.size());
    int need = 0;
    for (size_t i = 0; i < picks.size(); i++) {
        if (picks[i].kind == EditPick::OBJECT) out[i] = picks[i].index;
        else if (picks[i].kind == EditPick::BASE && baseToObject(picks[i], made[i].first, made[i].second)) {
            if (wants && !wants(made[i].first)) {
                if (left) (*left)[i] = 1;
            } else {
                need++, out[i] = -2;
            }
        }
    }
    if (!need) {
        for (int& k : out)
            if (k == -2) k = -1;
        return out;
    }
    beginEdit("MAKE MAP OBJECTS MINE");
    touchMeta();
    for (size_t i = 0; i < picks.size(); i++)
        if (out[i] == -2) {
            level.removed.push_back(made[i].second);
            level.objects.push_back(made[i].first);
            out[i] = (int)level.objects.size() - 1;
        }
    rebuildObjects();
    endEdit();
    return out;
}

void EditCore::removePicks(const std::vector<EditPick>& picks) {
    if (!world || picks.empty()) return;
    std::vector<int> objs;
    std::vector<V3> keys;
    for (const EditPick& p : picks) {
        if (p.kind == EditPick::OBJECT && p.index >= 0 && p.index < (int)level.objects.size()) objs.push_back(p.index);
        else if (p.kind == EditPick::BASE && p.baseKind >= 0 && (p.baseKind < 5 || p.baseKind == 6)) keys.push_back(V3(p.at.x, p.baseKind + 1, p.at.z));
    }
    if (objs.empty() && keys.empty()) return;
    std::sort(objs.begin(), objs.end());
    objs.erase(std::unique(objs.begin(), objs.end()), objs.end());
    beginEdit(objs.size() + keys.size() > 1 ? "DELETE " + std::to_string(objs.size() + keys.size()) + " OBJECTS" : "DELETE");
    touchMeta();
    for (auto it = objs.rbegin(); it != objs.rend(); ++it) level.objects.erase(level.objects.begin() + *it);
    for (const V3& k : keys) level.removed.push_back(k);
    rebuildObjects();
    endEdit();
}

// ---- Moving, turning, scaling ------------------------------------------------------------------

LevelObject EditCore::normalized(int k) const {
    LevelObject o = level.objects[k];
    if (o.kind == OBJ_ROCK && o.sub != 1) {  // left to chance: pin it as it was built
        for (int i = 0; i < (int)owner[0].size() && i < (int)world->rocks.size(); i++)
            if (owner[0][i] == k) {
                LevelObject f;
                if (rockToObject(world->rocks[i], f)) f.seed = o.seed, f.col[0] = o.col[0], o = f;
                break;
            }
    }
    if (o.kind == OBJ_BOX || o.kind == OBJ_LOG) {  // (standing where the ground is now)
        if (o.rel) resolveObject(*world, o);
        else settleObject(*world, o);
    }
    return o;
}

LevelObject EditCore::movedBy(LevelObject o, double dx, double dz) const {
    const World& w = *world;
    if (isSpan(o.kind)) {  // each end keeps its clearance over the ground it was on
        for (V3* e : {&o.p, &o.p2}) {
            const double clear = e->y - w.height(e->x, e->z);
            e->x += dx, e->z += dz;
            e->y = w.height(e->x, e->z) + clear;
        }
        settleObject(w, o);
        return o;
    }
    o.p.x += dx, o.p.z += dz;
    return grounded(o);
}

bool EditCore::rayObjects(const V3& o, const V3& d, double tMax, const std::vector<int>& skip, V3& p, V3& n) const {
    if (!world) return false;
    const World& w = *world;
    auto skipped = [&](int kind, int i) {
        const int ow = ownerOf(kind, i);
        return ow >= 0 && std::find(skip.begin(), skip.end(), ow) != skip.end();
    };
    double best = tMax;
    bool got = false;
    auto take = [&](const objshape::Hit& h) {
        if (h.t >= best) return;
        best = h.t, p = h.p, n = h.n, got = true;
    };
    objshape::Hit h;
    for (int i = 0; i < (int)w.rocks.size(); i++)
        if (!skipped(0, i) && objshape::rayRock(w.rocks[i], o, d, best, h)) take(h);
    for (int i = 0; i < (int)w.logs.size(); i++)
        if (!skipped(2, i) && objshape::rayLog(w.logs[i], o, d, best, h)) take(h);
    for (int i = 0; i < (int)w.boxes.size(); i++)
        if (w.boxes[i].solid && !skipped(3, i) && objshape::rayBox(w.boxes[i], o, d, best, h)) take(h);
    return got;
}

double EditCore::seatedTop(LevelObject o) {
    World& w = *world;
    o.kind = OBJ_ROCK, o.top = 0, o.sub = 1;
    const size_t n = w.rocks.size();
    placeObject(o, w);
    double lo = 1e9, hi = -1e9;
    for (const V3& v : w.rocks.back().v) lo = std::min(lo, v.y), hi = std::max(hi, v.y);
    w.rocks.resize(n);
    return 0.85 * (hi - lo);
}

LevelObject EditCore::transformed(LevelObject o, ObjOp op, double a) {
    const World& w = *world;
    switch (o.kind) {
        case OBJ_ROCK:
            o.sub = 1;
            switch (op) {
                case OP_YAW: o.yaw += a; break;
                case OP_PITCH: o.pitch += a; break;
                case OP_ROLL: o.roll += a; break;
                case OP_LIFT: o.top += a; break;
                case OP_SCALE: o.size = o.size * a, o.top *= a; break;
                case OP_STRETCH_X: o.size.x *= a; break;
                case OP_STRETCH_Y: o.size.y *= a, o.top *= a; break;
                case OP_STRETCH_Z: o.size.z *= a; break;
                case OP_SEAT: o.top = seatedTop(o); break;
            }
            o.size = V3(clampd(o.size.x, 0.15, 14), clampd(o.size.y, 0.1, 10), clampd(o.size.z, 0.15, 14));
            return o;
        case OBJ_BOX: {
            // Tipping turns it about its middle and leaves it at the height it was set: the offset is worked out afresh for the new tilt
            // (a block resting on the ground stays resting, as a ramp does).
            auto tip = [&](double& angle) {
                const double keep = grounded(o).p.y;
                angle = clampd(angle + a, -1.3, 1.3);
                if (std::fabs(o.lift) > 1e-6) {
                    LevelObject g = o;
                    g.lift = 0;
                    o.lift = keep - boxRestY(w, g);
                }
            };
            switch (op) {
                case OP_YAW: o.yaw += a; break;
                case OP_PITCH: tip(o.pitch); break;
                case OP_ROLL: tip(o.roll); break;
                case OP_LIFT: o.lift += a; break;
                case OP_SCALE: o.size = o.size * a; break;
                case OP_STRETCH_X: o.size.x *= a; break;
                case OP_STRETCH_Y: o.size.y *= a; break;
                case OP_STRETCH_Z: o.size.z *= a; break;
                case OP_SEAT: o.lift = 0; break;
            }
            o.size = V3(clampd(o.size.x, 0.05, 60), clampd(o.size.y, 0.03, 20), clampd(o.size.z, 0.05, 60));
            return grounded(o);
        }
        case OBJ_LOG: {
            V3 m = (o.p + o.p2) * 0.5, half = (o.p2 - o.p) * 0.5;
            double r = o.size.x;
            switch (op) {
                case OP_YAW: half = V3(half.x * std::cos(a) + half.z * std::sin(a), half.y, -half.x * std::sin(a) + half.z * std::cos(a)); break;
                case OP_PITCH: {
                    const double hor = std::hypot(half.x, half.z), L = std::hypot(hor, half.y);
                    const double e = clampd(std::atan2(half.y, hor) + a, -1.3, 1.3);
                    const V3 u = hor > 1e-6 ? V3(half.x / hor, 0, half.z / hor) : V3(1, 0, 0);
                    half = u * (L * std::cos(e)) + V3(0, L * std::sin(e), 0);
                    break;
                }
                case OP_ROLL: break;
                case OP_LIFT: m.y += a; break;
                case OP_SCALE: half = half * a, r *= a; break;
                case OP_STRETCH_X: half = half * a; break;
                case OP_STRETCH_Y:
                case OP_STRETCH_Z: r *= a; break;
                case OP_SEAT: {  // each end lying on the ground it's over
                    const double ra = clampd(r, 0.05, 2.0);
                    o.size.x = ra, o.lift = o.lift2 = 0;
                    return grounded(o);
                }
            }
            const double L = len(half);
            if (L < 0.5) half = half * (0.5 / std::max(L, 1e-6));
            if (L > 40) half = half * (40 / L);
            o.p = m - half, o.p2 = m + half, o.size.x = clampd(r, 0.05, 2.0);
            settleObject(w, o);  // (each end's clearance over the ground it's over, from where they ended up)
            return o;
        }
        case OBJ_FENCE: {
            const V3 m = (o.p + o.p2) * 0.5;
            V3 half = (o.p2 - o.p) * 0.5;
            switch (op) {
                case OP_YAW: half = V3(half.x * std::cos(a) + half.z * std::sin(a), half.y, -half.x * std::sin(a) + half.z * std::cos(a)); break;
                case OP_SCALE:
                case OP_STRETCH_X: half = half * a; break;
                default: break;
            }
            const double L = len(half);
            if (L < 0.5) half = half * (0.5 / std::max(L, 1e-6));
            if (L > 60) half = half * (60 / L);
            o.p = m - half, o.p2 = m + half;
            return o;
        }
        case OBJ_BUSH:
            switch (op) {
                case OP_LIFT: o.lift += a; break;
                case OP_SEAT: o.lift = 0; break;
                case OP_SCALE:
                case OP_STRETCH_X:
                case OP_STRETCH_Y:
                case OP_STRETCH_Z: o.size.x *= a; break;
                default: break;
            }
            o.size.x = clampd(o.size.x, 0.2, 6);
            return o;
        case OBJ_TREE:
            switch (op) {
                case OP_LIFT: o.lift += a; break;
                case OP_SEAT: o.lift = 0; break;
                case OP_SCALE:
                case OP_STRETCH_Y: o.size.y *= a; break;
                default: break;
            }
            o.size.y = clampd(o.size.y, 2, 45);
            return o;
        case OBJ_STATION:
            if (op == OP_YAW) o.yaw += a;
            return o;
        case OBJ_MODEL: {
            V3 s(o.size.x > 0 ? o.size.x : 1, o.size.y > 0 ? o.size.y : 1, o.size.z > 0 ? o.size.z : 1);
            switch (op) {
                case OP_YAW: o.yaw += a; break;
                case OP_PITCH: o.pitch += a; break;
                case OP_ROLL: o.roll += a; break;
                case OP_LIFT: o.lift += a; break;
                case OP_SEAT: o.lift = 0; break;
                case OP_SCALE: s = s * a; break;
                case OP_STRETCH_X: s.x *= a; break;
                case OP_STRETCH_Y: s.y *= a; break;
                case OP_STRETCH_Z: s.z *= a; break;
                default: break;
            }
            o.size = V3(clampd(s.x, 0.2, 8), clampd(s.y, 0.2, 8), clampd(s.z, 0.2, 8));
            return o;
        }
        default: break;
    }
    (void)w;
    return o;
}

void EditCore::replaceObjects(const std::vector<std::pair<int, LevelObject>>& set, const std::string& what, const std::string& key) {
    if (!world || set.empty()) return;
    beginEdit(what, key);
    touchMeta();
    for (const auto& s : set)
        if (s.first >= 0 && s.first < (int)level.objects.size()) level.objects[s.first] = s.second;
    rebuildObjects();
    endEdit();
}

void EditCore::moveObjects(const std::vector<int>& ks, double dx, double dz, const std::string& key) {
    std::vector<std::pair<int, LevelObject>> set;
    for (int k : ks)
        if (k >= 0 && k < (int)level.objects.size()) set.push_back({k, movedBy(normalized(k), dx, dz)});
    replaceObjects(set, "MOVE", key);
}

void EditCore::transformObjects(const std::vector<int>& ks, ObjOp op, double amount, const std::string& key) {
    static const char* names[] = {"TURN", "TIP", "ROLL", "RAISE", "SCALE", "STRETCH", "STRETCH", "STRETCH", "SEAT"};
    std::vector<int> v;
    for (int k : ks)
        if (k >= 0 && k < (int)level.objects.size()) v.push_back(k);
    if (v.empty()) return;
    // Turning or scaling several: they swing (or spread) round their middle, each also turning (or growing) in place.
    V3 c;
    for (int k : v) c += isSpan(level.objects[k].kind) ? (level.objects[k].p + level.objects[k].p2) * 0.5 : level.objects[k].p;
    c = c * (1.0 / v.size());
    std::vector<std::pair<int, LevelObject>> set;
    for (int k : v) {
        LevelObject o = normalized(k);
        if (op == OP_YAW && v.size() > 1) {
            auto swing = [&](V3 p) {
                const double x = p.x - c.x, z = p.z - c.z;
                return V3(c.x + x * std::cos(amount) + z * std::sin(amount), p.y, c.z - x * std::sin(amount) + z * std::cos(amount));
            };
            if (isSpan(o.kind)) {
                const V3 a = swing(o.p), b = swing(o.p2);
                const World& w = *world;
                const double ca = o.p.y - w.height(o.p.x, o.p.z), cb = o.p2.y - w.height(o.p2.x, o.p2.z);
                o.p = V3(a.x, w.height(a.x, a.z) + ca, a.z), o.p2 = V3(b.x, w.height(b.x, b.z) + cb, b.z);
                settleObject(w, o);
            } else {
                const V3 q = swing(o.p);
                o = movedBy(o, q.x - o.p.x, q.z - o.p.z);
                o = transformed(o, op, amount);
            }
        } else if (op == OP_SCALE && v.size() > 1) {
            // Scaling several: the group grows as a whole, each object's place scaled about the middle as well as its size.
            const V3 m = isSpan(o.kind) ? (o.p + o.p2) * 0.5 : o.p;
            const V3 q = c + (m - c) * amount;
            o = movedBy(o, q.x - m.x, q.z - m.z);
            o = transformed(o, op, amount);
        } else {
            o = transformed(o, op, amount);
        }
        set.push_back({k, o});
    }
    replaceObjects(set, names[op], key);
}

V3 axisDir(EditAxis a) { return a == AXIS_X ? V3(1, 0, 0) : a == AXIS_Y ? V3(0, 1, 0) : a == AXIS_Z ? V3(0, 0, 1) : V3(0, 0, 0); }
const char* axisName(EditAxis a) { return a == AXIS_X ? "X" : a == AXIS_Y ? "Y" : a == AXIS_Z ? "Z" : "-"; }

// ---- Dragging with the mouse ---------------------------------------------------------------------

void EditCam::ray(double sx, double sy, V3& o, V3& d) const {
    const V3 f = forward(), rt = right(), up = cross(rt, f);
    const double k = std::tan(fovy / 2);
    const double dx = (sx - W / 2.0) / (H / 2.0) * k, dy = (H / 2.0 - sy) / (H / 2.0) * k;
    o = eye;
    d = norm(f + rt * dx + up * dy);
}

bool EditCam::project(const V3& p, double& sx, double& sy) const {
    const V3 f = forward(), rt = right(), up = cross(rt, f), d = p - eye;
    const double z = dot(d, f);
    if (z < 0.05) return false;
    const double k = 1.0 / std::tan(fovy / 2);
    sx = W / 2.0 + dot(d, rt) / z * k * H / 2.0;
    sy = H / 2.0 - dot(d, up) / z * k * H / 2.0;
    return true;
}

double EditCam::pxPerMetre(const V3& p) const { return H / 2.0 / std::tan(fovy / 2) / std::max(0.3, dot(p - eye, forward())); }

bool rayLineClosest(const V3& o, const V3& d, const V3& p, const V3& a, double& s) {
    const V3 w0 = o - p;
    const double b = dot(d, a), dd = dot(d, w0), e = dot(a, w0), den = 1 - b * b;
    if (den < 1e-5) return false;
    if ((b * e - dd) / den <= 0) return false;  // (the nearest point of the ray is behind the eye)
    s = (e - b * dd) / den;
    return true;
}

bool lineUnderMouse(const EditCam& cam, double sx, double sy, const V3& p, const V3& a, double& s) {
    const double k = std::max(0.05, 0.1 * len(p - cam.eye));
    double ax, ay, bx, by;
    if (!cam.project(p, ax, ay) || !cam.project(p + a * k, bx, by)) return false;
    const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    if (l2 < 4) return false;  // (under 2 px for a tenth of the way to it: pointing at the eye)
    const double u = ((sx - ax) * dx + (sy - ay) * dy) / l2;
    V3 o, d;
    cam.ray(ax + dx * u, ay + dy * u, o, d);
    return rayLineClosest(o, d, p, a, s);
}

bool EditCore::turnsFreely(int kind) { return kind == OBJ_ROCK || kind == OBJ_BOX || kind == OBJ_LOG || kind == OBJ_MODEL; }
bool EditCore::turnsAboutY(int kind) { return turnsFreely(kind) || kind == OBJ_STATION || kind == OBJ_FENCE; }
bool EditCore::canLift(int kind) { return kind != OBJ_STATION && kind != OBJ_FENCE; }

V3 EditCore::footOf(const LevelObject& o) const {
    const World& w = *world;
    switch (o.kind) {
        case OBJ_ROCK: return V3(o.p.x, w.height(o.p.x, o.p.z) + o.top, o.p.z);
        case OBJ_BOX: return grounded(o).p;
        case OBJ_LOG: return (o.p + o.p2) * 0.5;
        case OBJ_FENCE: return V3((o.p.x + o.p2.x) / 2, (w.height(o.p.x, o.p.z) + w.height(o.p2.x, o.p2.z)) / 2, (o.p.z + o.p2.z) / 2);
        case OBJ_STATION: return V3(o.p.x, w.height(o.p.x, o.p.z), o.p.z);
        default: return V3(o.p.x, w.height(o.p.x, o.p.z) + o.lift, o.p.z);
    }
}

double EditCore::rideAt(double x, double z, const std::vector<int>& skip) const {
    if (!world) return 0;
    const double g = world->height(x, z);
    V3 p, n;
    if (rayObjects(V3(x, g + 60, z), V3(0, -1, 0), 60, skip, p, n) && n.y > 0.25) return std::max(0.0, p.y - g);
    return 0;
}

EditCore::Slide EditCore::slideUnder(const LevelObject& lead, const V3& grab, const V3& o, const V3& d, const std::vector<int>& skip, double ride0, double near,
                                     double snap) const {
    Slide out;
    if (!world) return out;
    const World& w = *world;
    const V3 foot0 = footOf(lead);
    const V3 off(grab.x - foot0.x, 0, grab.z - foot0.z);  // (the grabbed point from the lead's middle, in plan)
    const bool lifts = canLift(lead.kind);
    // What it could ride on: the solid things whose plan circle comes near the line its middle sweeps as the grabbed point runs down the ray.
    struct Cand {
        int kind, index;  // (0 rock, 2 log, 3 box)
        double t0, t1;    // where along the ray its middle is over it
    };
    std::vector<Cand> cand;
    if (lifts) {
        const double ox = o.x - off.x, oz = o.z - off.z, hx = d.x, hz = d.z, hh = hx * hx + hz * hz;
        double t0 = 0, t1 = 0;
        auto near2 = [&](double cx, double cz, double r) {
            r += 0.05;
            if (hh < 1e-12) {  // (straight down: the middle stays put)
                t0 = -1e18, t1 = 1e18;
                return std::hypot(ox - cx, oz - cz) < r;
            }
            const double tc = ((cx - ox) * hx + (cz - oz) * hz) / hh, off2 = std::hypot(ox + hx * tc - cx, oz + hz * tc - cz);
            if (off2 >= r) return false;
            const double half = std::sqrt(r * r - off2 * off2) / std::sqrt(hh);
            t0 = tc - half, t1 = tc + half;
            return t1 > 0;
        };
        auto mine = [&](int kind, int i) {
            const int ow = ownerOf(kind, i);
            return ow >= 0 && std::find(skip.begin(), skip.end(), ow) != skip.end();
        };
        for (int i = 0; i < (int)w.rocks.size(); i++)
            if (near2(w.rocks[i].c.x, w.rocks[i].c.z, w.rocks[i].r) && !mine(0, i)) cand.push_back({0, i, t0, t1});
        for (int i = 0; i < (int)w.logs.size(); i++) {
            const Log& g = w.logs[i];
            if (near2((g.a.x + g.b.x) / 2, (g.a.z + g.b.z) / 2, len(g.b - g.a) / 2 + g.r) && !mine(2, i)) cand.push_back({2, i, t0, t1});
        }
        for (int i = 0; i < (int)w.boxes.size(); i++)
            if (w.boxes[i].solid && near2(w.boxes[i].c.x, w.boxes[i].c.z, w.boxes[i].bound) && !mine(3, i)) cand.push_back({3, i, t0, t1});
    }
    auto rideHere = [&](double x, double z, double t) {
        if (cand.empty()) return 0.0;
        const double g = w.height(x, z);
        const V3 from(x, g + 60, z), down(0, -1, 0);
        double best = 60;
        V3 n;
        objshape::Hit h;
        for (const Cand& c : cand) {
            if (t < c.t0 - 0.5 || t > c.t1 + 0.5) continue;  // (not over it here)
            bool hit = false;
            if (c.kind == 0) hit = objshape::rayRock(w.rocks[c.index], from, down, best, h);
            else if (c.kind == 2) hit = objshape::rayLog(w.logs[c.index], from, down, best, h);
            else hit = objshape::rayBox(w.boxes[c.index], from, down, best, h);
            if (hit) best = h.t, n = h.n;
        }
        return best < 60 && n.y > 0.25 ? std::max(0.0, 60 - best) : 0.0;
    };
    // How far the grabbed point is above (+) or below its place if the mouse ray put it `t` along: the root is where it sits on the ray.
    auto at = [&](double dx, double dz, double t, Slide& sl) {
        const V3 f = footOf(movedBy(lead, dx, dz));
        sl.dx = dx, sl.dz = dz;
        sl.ride = lifts ? rideHere(f.x, f.z, t) - ride0 : 0;
        sl.rise = f.y - foot0.y + sl.ride;
    };
    auto gap = [&](double t, Slide& sl) {
        const V3 x = o + d * t;
        at(x.x - grab.x, x.z - grab.z, t, sl);
        sl.t = t;
        return x.y - (grab.y + sl.rise);
    };
    Slide tmp;
    std::vector<double> roots;
    double t = 0.2, f0 = gap(t, tmp);
    while (t < 3000 && roots.size() < 32) {
        const double t1 = t + std::max(0.1, t * 0.01), f1 = gap(t1, tmp);
        if ((f0 > 0) != (f1 > 0)) {
            double a = t, b = t1, fa = f0;
            for (int k = 0; k < 40; k++) {
                const double m = (a + b) / 2, fm = gap(m, tmp);
                if ((fm > 0) == (fa > 0)) a = m, fa = fm;
                else b = m;
            }
            roots.push_back((a + b) / 2);
        }
        t = t1, f0 = f1;
    }
    if (roots.empty()) return out;
    double best = roots[0];
    for (double r : roots)
        if (std::fabs(r - near) < std::fabs(best - near)) best = r;
    gap(best, out);
    out.ok = true;
    if (snap > 0) {  // (the lead's middle on the grid: the grabbed point leaves the mouse by up to half a step)
        const double nx = std::round((foot0.x + out.dx) / snap) * snap, nz = std::round((foot0.z + out.dz) / snap) * snap;
        cand.clear();  // (off the ray's line: ask everything)
        const V3 f = footOf(movedBy(lead, nx - foot0.x, nz - foot0.z));
        out.dx = nx - foot0.x, out.dz = nz - foot0.z;
        out.ride = lifts ? rideAt(f.x, f.z, skip) - ride0 : 0;
        out.rise = f.y - foot0.y + out.ride;
    }
    return out;
}

LevelObject EditCore::slid(const LevelObject& o, double dx, double dz, double rise) {
    LevelObject m = movedBy(o, dx, dz);
    if (std::fabs(rise) > 1e-9 && canLift(o.kind)) m = transformed(m, OP_LIFT, rise);
    return m;
}

LevelObject EditCore::translated(LevelObject o, const V3& d) const {
    const World& w = *world;
    const double g0 = w.height(o.p.x, o.p.z);
    switch (o.kind) {
        case OBJ_BOX:
            o.p += d;
            settleObject(w, o);
            return o;
        case OBJ_LOG:
            o.p += d, o.p2 += d;
            settleObject(w, o);
            return o;
        case OBJ_FENCE:
            o.p.x += d.x, o.p.z += d.z, o.p2.x += d.x, o.p2.z += d.z;
            return o;
        default: {
            o.p.x += d.x, o.p.z += d.z;
            const double g1 = w.height(o.p.x, o.p.z), up = d.y - (g1 - g0);  // (what it rises over the ground there)
            o.p.y = g1;
            if (o.kind == OBJ_ROCK) o.top += up;
            else if (canLift(o.kind)) o.lift += up;
            return o;
        }
    }
}

V3 EditCore::rockAt(const LevelObject& o) {
    World& w = *world;
    const size_t n = w.rocks.size();
    placeObject(o, w);
    const V3 at = w.rocks.size() > n ? w.rocks.back().at : V3(o.p.x, w.height(o.p.x, o.p.z), o.p.z);
    w.rocks.resize(n);
    return at;
}

namespace {
double turnedYaw(const Quat& q, double yaw) {  // (the heading after the turn, for things that stay upright)
    const V3 f = q.rotate(V3(std::cos(yaw), 0, -std::sin(yaw)));
    return std::hypot(f.x, f.z) > 1e-6 ? std::atan2(-f.z, f.x) : yaw;
}
void turnFrame(LevelObject& o, const Quat& q) {
    V3 ax[3];
    frameOf(o.yaw, o.pitch, o.roll, ax);
    for (V3& a : ax) a = q.rotate(a);
    anglesOf(ax, o.yaw, o.pitch, o.roll, o.yaw);
}
}  // namespace

LevelObject EditCore::rotatedAbout(LevelObject o, const Quat& qq, const V3& c, const V3* bodyAt) {
    const World& w = *world;
    // (Things that stay upright take only the turn about the vertical: the twist of q about Y. Tipped right over, none.)
    Quat q = qq;
    if (!turnsFreely(o.kind) || o.name == "menu_vehicle") {  // (a parked vehicle stands on its wheels: only the turn about the vertical)
        q = std::hypot(qq.w, qq.y) > 1e-6 ? Quat{qq.w, 0, qq.y, 0} : Quat();
        q.normalize();
    }
    auto spin = [&](const V3& x) { return c + q.rotate(x - c); };
    switch (o.kind) {
        case OBJ_BOX:
            o.p = spin(o.p);
            turnFrame(o, q);
            settleObject(w, o);  // (it stays exactly where the turn put it: the offset over the ground is worked out for the new tilt)
            return o;
        case OBJ_LOG:
            o.p = spin(o.p), o.p2 = spin(o.p2);
            settleObject(w, o);
            return o;
        case OBJ_ROCK: {
            const V3 at = bodyAt ? *bodyAt : rockAt(o), to = spin(at);
            turnFrame(o, q);
            o.sub = 1;
            o.p = V3(to.x, w.height(to.x, to.z), to.z);
            o.top = 0;
            o.top = to.y - rockAt(o).y;  // (`top` is the rock's highest point over the ground: whatever turning it brought up)
            return o;
        }
        case OBJ_STATION: {
            const V3 to = spin(V3(o.p.x, w.height(o.p.x, o.p.z), o.p.z));
            o.p = V3(to.x, w.height(to.x, to.z), to.z);
            o.yaw = turnedYaw(q, o.yaw);
            return o;
        }
        case OBJ_FENCE: {
            const V3 a = spin(o.p), b = spin(o.p2);
            o.p.x = a.x, o.p.z = a.z, o.p2.x = b.x, o.p2.z = b.z;
            return o;
        }
        default: {  // a structure (turned in its frame), or a tree or bush (upright, turning about Y round the pivot): its origin goes round, and stands that high
            const V3 to = spin(V3(o.p.x, w.height(o.p.x, o.p.z) + o.lift, o.p.z));
            const double g = w.height(to.x, to.z);
            o.p = V3(to.x, g, to.z);
            if (o.kind == OBJ_MODEL) turnFrame(o, q), o.lift = to.y - g;  // (where the turn put it; an upright thing keeps its footing)
            return o;
        }
    }
}

bool EditCore::middleOf(const std::vector<int>& ks, V3& mid, double* radius) const {
    V3 lo(1e18, 1e18, 1e18), hi(-1e18, -1e18, -1e18);
    for (int k : ks)
        for (const auto& part : partsOf(k)) {
            const Shape sh = shapeOf(part.first, part.second);
            for (const auto& e : sh.edges)
                for (const V3* q : {&e.first, &e.second}) lo = V3(std::min(lo.x, q->x), std::min(lo.y, q->y), std::min(lo.z, q->z)), hi = V3(std::max(hi.x, q->x), std::max(hi.y, q->y), std::max(hi.z, q->z));
        }
    if (lo.x > hi.x) return false;
    mid = (lo + hi) * 0.5;
    if (radius) *radius = len(hi - lo) / 2;
    return true;
}

void EditCore::putObjects(const std::vector<std::pair<int, LevelObject>>& set, const std::string& what, const std::string& key) { replaceObjects(set, what, key); }

void EditCore::cancelEdit() {
    if (depth <= 0) return;
    depth = 0;
    for (auto it = pending.cells.rbegin(); it != pending.cells.rend(); ++it) setCell(it->id, it->what, it->worldOld, it->hadOld, it->lvOld);
    if (pending.meta) restoreMeta(pending.before);
    else if (!pending.cells.empty() && !level.objects.empty()) rebuildObjects();
    pending = Entry();
    pendingCells.clear();
}

// ---- Outlines ----------------------------------------------------------------------------------

std::vector<std::pair<int, int>> EditCore::partsOf(int k) const {
    std::vector<std::pair<int, int>> out;
    for (int q = 0; q < 7; q++)
        for (int i = 0; i < (int)owner[q].size(); i++)
            if (owner[q][i] == k) out.push_back({q, i});
    return out;
}

Shape EditCore::shapeOf(int baseKind, int index) const {
    Shape s;
    if (!world || index < 0) return s;
    const World& w = *world;
    switch (baseKind) {
        case 0: {
            if (index >= (int)w.rocks.size()) break;
            const Rock& r = w.rocks[index];
            std::vector<std::pair<int, int>> seen;
            for (const auto& f : r.f) {
                s.tris.push_back({r.v[f[0]], r.v[f[1]], r.v[f[2]]});
                for (int e = 0; e < 3; e++) {
                    int a = f[e], b = f[(e + 1) % 3];
                    if (a > b) std::swap(a, b);
                    seen.push_back({a, b});
                }
            }
            std::sort(seen.begin(), seen.end());
            seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
            for (const auto& e : seen) s.edges.push_back({r.v[e.first], r.v[e.second]});
            break;
        }
        case 1: {
            if (index >= (int)w.trees.size()) break;
            const Tree& t = w.trees[index];
            s.edges.push_back({t.base, t.base + V3(0, t.height * (t.kind == 2 ? 0.95 : 0.6), 0)});
            if (t.kind == 0) {
                const V3 b = t.base + V3(0, t.height * 0.18, 0), apex = t.base + V3(0, t.height, 0);
                ringEdges(s, b, V3(1, 0, 0), V3(0, 0, 1), t.height * 0.17, 16);
                for (int k = 0; k < 4; k++) s.edges.push_back({b + V3(std::cos(k * M_PI / 2), 0, std::sin(k * M_PI / 2)) * (t.height * 0.17), apex});
            } else if (t.kind == 1) {
                const V3 c = t.base + V3(0, t.height * 0.64, 0);
                const double r = t.height * 0.28;
                ringEdges(s, c, V3(1, 0, 0), V3(0, 0, 1), r, 16);
                ringEdges(s, c, V3(1, 0, 0), V3(0, 1, 0), r, 16);
                ringEdges(s, c, V3(0, 0, 1), V3(0, 1, 0), r, 16);
            }
            break;
        }
        case 2: {
            if (index >= (int)w.logs.size()) break;
            const Log& g = w.logs[index];
            const V3 ax = norm(g.b - g.a), u = norm(cross(ax, std::fabs(ax.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0))), v = cross(ax, u);
            ringEdges(s, g.a, u, v, g.r, 10);
            ringEdges(s, g.b, u, v, g.r, 10);
            for (int k = 0; k < 4; k++) {
                const V3 off = u * (std::cos(k * M_PI / 2) * g.r) + v * (std::sin(k * M_PI / 2) * g.r);
                s.edges.push_back({g.a + off, g.b + off});
            }
            break;
        }
        case 3:
            if (index < (int)w.boxes.size() && !w.boxes[index].hidden) boxEdges(w.boxes[index], s, true);
            break;
        case 4: {
            if (index >= (int)w.fixtures.size()) break;
            const Prop& f = w.fixtures[index];
            Placed pl;
            int sub;
            if (f.kind == PROP_GARAGE) boxEdges(shedBox(f), s, false);
            else if (f.kind == PROP_FENCE) boxEdges(fenceBox(f), s, false);
            else if (propAsModel(f, index, pl, sub)) boxEdges(modelBox(pl), s, false);
            else s.edges.push_back({f.a, f.a + V3(0, 1.6, 0)});
            break;
        }
        case 6: {
            if (index >= (int)w.bushes.size()) break;
            const Bush& b = w.bushes[index];
            const PlantInfo& pi = plantInfo(b.kind);
            const V3 c = b.p + V3(0, b.r * pi.cy, 0);
            ringEdges(s, c, V3(1, 0, 0), V3(0, 0, 1), b.r * pi.rx, 14);
            ringEdges(s, c, V3(1, 0, 0), V3(0, pi.ry / pi.rx, 0), b.r * pi.rx, 14);
            ringEdges(s, c, V3(0, 0, 1), V3(0, pi.ry / pi.rx, 0), b.r * pi.rx, 14);
            break;
        }
        case 5:
            if (index < (int)w.models.size()) boxEdges(modelBox(w.models[index]), s, false);
            break;
        default: break;
    }
    return s;
}

Shape EditCore::previewShape(const LevelObject& o) {
    Shape s;
    if (!world) return s;
    World& w = *world;
    if (o.kind == OBJ_STATION) {  // (not placed even for a moment: a station keeps its name for good)
        Prop f;
        f.kind = PROP_GARAGE, f.a = V3(o.p.x, w.height(o.p.x, o.p.z), o.p.z), f.yaw = o.yaw;
        boxEdges(shedBox(f), s, false);
        return s;
    }
    const size_t n[9] = {w.rocks.size(), w.trees.size(), w.poles.size(), w.logs.size(), w.boxes.size(), w.fixtures.size(), w.stations.size(), w.models.size(), w.bushes.size()};
    placeObject(o, w);
    for (size_t i = n[0]; i < w.rocks.size(); i++) s.add(shapeOf(0, (int)i));
    for (size_t i = n[1]; i < w.trees.size(); i++) s.add(shapeOf(1, (int)i));
    for (size_t i = n[3]; i < w.logs.size(); i++) s.add(shapeOf(2, (int)i));
    for (size_t i = n[4]; i < w.boxes.size(); i++) s.add(shapeOf(3, (int)i));
    for (size_t i = n[5]; i < w.fixtures.size(); i++) s.add(shapeOf(4, (int)i));
    for (size_t i = n[7]; i < w.models.size(); i++) s.add(shapeOf(5, (int)i));
    for (size_t i = n[8]; i < w.bushes.size(); i++) s.add(shapeOf(6, (int)i));
    w.bushes.resize(n[8]);
    w.rocks.resize(n[0]), w.trees.resize(n[1]), w.poles.resize(n[2]), w.logs.resize(n[3]);
    w.boxes.resize(n[4]), w.fixtures.resize(n[5]), w.stations.resize(n[6]), w.models.resize(n[7]);
    return s;
}

// ---- The course ------------------------------------------------------------------------

void EditCore::setStart(double x, double z, double yaw) {
    if (!world) return;
    if (level.section.on) x = std::round(x), z = std::round(z), yaw = level.section.heading;  // (the entry is on a whole metre: the frame's samples are cells)
    beginEdit("MOVE START");
    touchMeta();
    level.hasStart = true;
    level.start = V3(x, world->height(x, z), z);
    level.startYaw = wrapAngle(yaw);
    syncSpawn();
    endEdit();
}

int EditCore::addGate(const Gate& g, int at) {
    if (!world) return -1;
    beginEdit("ADD GATE");
    touchMeta();
    if (at < 0 || at > (int)level.gates.size()) at = (int)level.gates.size();
    Gate q = g;
    q.p.y = world->height(q.p.x, q.p.z);
    q.yaw = wrapAngle(q.yaw);
    level.gates.insert(level.gates.begin() + at, q);
    endEdit();
    return at;
}

void EditCore::setGate(int k, const Gate& g, const std::string& mergeKey) {
    if (!world || k < 0 || k >= (int)level.gates.size()) return;
    beginEdit("CHANGE GATE", mergeKey);
    touchMeta();
    Gate q = g;
    q.p.y = world->height(q.p.x, q.p.z);
    q.yaw = wrapAngle(q.yaw);
    level.gates[k] = q;
    endEdit();
}

void EditCore::removeGate(int k) {
    if (!world || k < 0 || k >= (int)level.gates.size()) return;
    beginEdit("DELETE GATE");
    touchMeta();
    level.gates.erase(level.gates.begin() + k);
    endEdit();
}

void EditCore::moveGate(int k, int dir) {
    const int to = k + dir;
    if (!world || k < 0 || k >= (int)level.gates.size() || to < 0 || to >= (int)level.gates.size()) return;
    beginEdit("REORDER GATES");
    touchMeta();
    std::swap(level.gates[k], level.gates[to]);
    endEdit();
}

// ---- Sections --------------------------------------------------------------------------

void EditCore::clearBushes() {
    const SectionInfo& c = level.section;
    std::vector<Bush>& b = world->bushes;
    b.erase(std::remove_if(b.begin(), b.end(), [&](const Bush& k) { return c.outside(k.p.x, k.p.z) < 26; }), b.end());
}

void EditCore::newSection(const SectionInfo& canvas) {
    if (!world) return;
    World& w = *world;
    const double blend = 24;
    level.section = canvas;
    level.section.on = true;
    level.map = w.map;
    level.mode = LEVEL_SPRINT;
    const double ch = std::cos(canvas.heading), sh = std::sin(canvas.heading);
    const V3 fwd(ch, 0, -sh), left(sh, 0, ch), origin(canvas.x, 0, canvas.z);
    // The pad at the canvas's mean ground, to the half metre.
    double sum = 0;
    int n = 0;
    for (double u = 0; u <= canvas.length; u += 5)
        for (double v = -canvas.half; v <= canvas.half; v += 5) {
            const V3 p = origin + fwd * u + left * v;
            sum += w.height(p.x, p.z), n++;
        }
    const double padY = std::round(sum / std::max(1, n) * 2) / 2;
    beginEdit("NEW SECTION");
    touchMeta();
    levelPad(origin + fwd * (canvas.length / 2), canvas.heading, canvas.length / 2, canvas.half, padY, blend);
    // Whatever stood on it goes (the map's own, noted so the level plays the same): until nothing's left.
    for (int pass = 0; pass < 4; pass++) {
        std::vector<V3> gone;
        auto in = [&](double x, double z) { return canvas.outside(x, z) < blend + 1; };
        for (const Rock& r : w.rocks)
            if (in(r.c.x, r.c.z)) gone.push_back(V3(r.c.x, 0, r.c.z));
        for (const Tree& t : w.trees)
            if (in(t.base.x, t.base.z)) gone.push_back(V3(t.base.x, 0, t.base.z));
        for (const Log& g : w.logs) {
            const V3 m = (g.a + g.b) * 0.5;
            if (in(m.x, m.z)) gone.push_back(V3(m.x, 0, m.z));
        }
        for (const Box& b : w.boxes)
            if (!b.hidden && in(b.c.x, b.c.z)) gone.push_back(V3(b.c.x, 0, b.c.z));
        for (const Prop& f : w.fixtures)
            if (in(f.a.x, f.a.z)) gone.push_back(V3(f.a.x, 0, f.a.z));
        if (gone.empty()) break;
        level.removed.insert(level.removed.end(), gone.begin(), gone.end());
        rebuildObjects();
    }
    clearBushes();
    // The entry near the back, the exit near the far end: a section's run is the 180 m between.
    const double entryU = 40, exitU = std::max(entryU + 20, canvas.length - 40);
    setStart(origin.x + fwd.x * entryU, origin.z + fwd.z * entryU, canvas.heading);
    Gate g;
    g.p = origin + fwd * exitU;
    g.yaw = canvas.heading;
    g.width = 14;
    addGate(g);
    endEdit();
    history.clear();
    future.clear();
    pending = Entry();
    pendingCells.clear();
    changes = savedChanges = 0;
    dirtyCells.add(0, 0);
    dirtyCells.add(w.gridSize() - 1, w.gridSize() - 1);
    dirtyWater = dirtyObjects = true;
}

int EditCore::stampSection(int type, uint32_t seed) {
    if (!world || !level.section.on || !level.hasStart || type < 0 || type >= SEC_COUNT) return -1;
    const SectionType t = (SectionType)type;
    const SectionInfo& c = level.section;
    SectionFrame f;
    f.entry = V3(level.start.x, world->height(level.start.x, level.start.z), level.start.z);
    f.heading = c.heading;
    const SectionFootprint fp = sectionFootprint(t);
    // Built on a copy of the world, then the differences go through the edit (so undo has them).
    World scratch = *world;
    std::vector<LevelObject> made;
    const SectionBuilt built = buildSection(scratch, t, seed, f, &made);
    const double ch = std::cos(f.heading), sh = std::sin(f.heading);
    auto local = [&](double x, double z, double& u, double& v) {
        u = (x - f.entry.x) * ch - (z - f.entry.z) * sh;
        v = (x - f.entry.x) * sh + (z - f.entry.z) * ch;
    };
    auto inFootprint = [&](double x, double z) {
        double u, v;
        local(x, z, u, v);
        return std::max({-fp.apron - u, u - (fp.length + fp.apron), std::fabs(v) - fp.half}) < fp.blend + 1;
    };
    beginEdit(std::string("STAMP ") + sectionName(t));
    touchMeta();
    double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (double uu : {-fp.apron - fp.blend, fp.length + fp.apron + fp.blend})
        for (double vv : {-fp.half - fp.blend, fp.half + fp.blend}) {
            const double x = f.entry.x + ch * uu + sh * vv, z = f.entry.z - sh * uu + ch * vv;
            x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
        }
    for (int xi = (int)std::floor(x0); xi <= (int)std::ceil(x1); xi++)
        for (int zi = (int)std::floor(z0); zi <= (int)std::ceil(z1); zi++) {
            const int id = world->cellIndex(xi, zi);
            if (id < 0) continue;
            if (std::fabs(scratch.cellHeight(id) - world->cellHeight(id)) > 1e-5) touchHeight(id, scratch.cellHeight(id));
            const uint16_t sp = (uint16_t)(scratch.cellSurface(id) << 8 | scratch.cellPaint(id));
            if (sp != (uint16_t)(world->cellSurface(id) << 8 | world->cellPaint(id))) touchSurface(id, sp);
            if (!scratch.waterY.empty()) {
                const float wl = scratch.waterY[id];
                if (std::fabs(wl - worldCell(id, 2)) > 1e-4) touchWater(id, wl);
                // The current goes with it (the level keeps it; undo leaves it where there's no water: harmless, and not saved).
                level.flow.erase(id);
                const float fx = scratch.flowX[id], fz = scratch.flowZ[id];
                if (wl > -1e8f && (fx != 0 || fz != 0)) level.flow[id] = {fx, fz};
                if (!world->flowX.empty()) world->flowX[id] = wl > -1e8f ? fx : 0, world->flowZ[id] = wl > -1e8f ? fz : 0;
            }
        }
    // The level's own objects there go too (the generator clears what stands on its ground).
    level.objects.erase(std::remove_if(level.objects.begin(), level.objects.end(),
                                       [&](const LevelObject& o) {
                                           const V3 at = isSpan(o.kind) ? (o.p + o.p2) * 0.5 : o.p;
                                           return inFootprint(at.x, at.z);
                                       }),
                        level.objects.end());
    level.objects.insert(level.objects.end(), made.begin(), made.end());
    // The exit where the section ends.
    Gate g;
    g.p = built.exit;
    g.yaw = f.heading;
    g.width = 14;
    g.p.y = world->height(g.p.x, g.p.z);
    if (level.gates.empty()) level.gates.push_back(g);
    else level.gates.back() = g;
    static const int kindOf[SEC_COUNT] = {SK_CLIMB, SK_BOG, SK_CROSSING, SK_LEDGES, SK_DUNES, SK_SHELF, SK_TIMBER};
    if (level.section.kind == SK_CUSTOM) level.section.kind = kindOf[type];
    level.section.gen = type, level.section.genSeed = seed;
    rebuildObjects();
    endEdit();
    return (int)made.size();
}

// ---- Undo ------------------------------------------------------------------------------

bool EditCore::undo() {
    if (history.empty() || editing()) return false;
    Entry e = std::move(history.back());
    history.pop_back();
    for (auto it = e.cells.rbegin(); it != e.cells.rend(); ++it) setCell(it->id, it->what, it->worldOld, it->hadOld, it->lvOld);
    if (e.meta) restoreMeta(e.before);
    else if (!e.cells.empty() && !level.objects.empty()) rebuildObjects();
    future.push_back(std::move(e));
    changes++;
    return true;
}

bool EditCore::redo() {
    if (future.empty() || editing()) return false;
    Entry e = std::move(future.back());
    future.pop_back();
    for (const CellChange& c : e.cells) setCell(c.id, c.what, c.worldNew, c.hasNew, c.lvNew);
    if (e.meta) restoreMeta(e.after);
    else if (!e.cells.empty() && !level.objects.empty()) rebuildObjects();
    history.push_back(std::move(e));
    changes++;
    return true;
}

bool EditCore::save(const std::string& path, std::string* err) {
    Level out = level;
    out.builtin = false;
    if (!saveLevelFile(out, path, err)) return false;
    touchLevelFiles();  // (a proving ground built earlier is stale now)
    level.file = path;
    level.builtin = false;
    savedChanges = changes;
    // A section is also saved in its own frame, beside the level.
    saveNote.clear();
    if (level.section.on) {
        SectionFile sf;
        std::string why;
        if (!extractSection(*world, level, sf, &why)) saveNote = "NO SECTION FILE: " + why;
        else if (!saveSectionFile(sf, sectionPathFor(path), &why)) saveNote = "NO SECTION FILE: " + why;
        else if (sf.dropped) saveNote = std::to_string(sf.dropped) + " OBJECTS OUTSIDE THE CANVAS LEFT OUT OF THE SECTION FILE";
    }
    return true;
}

// ---- Looking -------------------------------------------------------------------------

bool EditCore::rayGround(const V3& o, const V3& d, double maxT, V3& hit) const {
    if (!world) return false;
    auto above = [&](double t) {
        const V3 p = o + d * t;
        return p.y - world->height(p.x, p.z);
    };
    double prev = 0, fa = above(0);
    if (fa < 0) return false;
    for (double t = 0.25; t <= maxT;) {
        const double f = above(t);
        if (f < 0) {
            double lo = prev, hi = t;
            for (int k = 0; k < 24; k++) {
                const double mid = (lo + hi) / 2;
                (above(mid) < 0 ? hi : lo) = mid;
            }
            hit = o + d * hi;
            return true;
        }
        prev = t;
        // Steps grow with distance and with height above the ground (never past it).
        t += clampd(std::min(f * 0.5, 0.004 * t + 0.25), 0.1, 4.0);
    }
    return false;
}

std::vector<EditPick> EditCore::pickInside(const std::function<bool(const V3&)>& inside, bool everything, size_t cap, bool* capped) const {
    std::vector<EditPick> found;
    if (capped) *capped = false;
    if (!world) return found;
    const World& w = *world;
    auto take = [&](int baseKind, int index, const V3& at, const V3& look, const std::string& name) {
        if (!inside(look)) return;
        EditPick p;
        const int ow = ownerOf(baseKind, index);
        p.kind = ow >= 0 ? EditPick::OBJECT : EditPick::BASE;
        p.index = ow >= 0 ? ow : index, p.baseKind = ow >= 0 ? -1 : baseKind, p.at = at, p.name = name;
        if (ow >= 0 && std::find(found.begin(), found.end(), p) != found.end()) return;
        if (ow < 0 && !canAdopt(p)) return;
        if (found.size() >= cap) {
            if (capped) *capped = true;
            return;
        }
        found.push_back(p);
    };
    for (int i = 0; i < (int)w.rocks.size(); i++) take(0, i, w.rocks[i].c, w.rocks[i].c, "ROCK");
    for (int i = 0; i < (int)w.trees.size(); i++) take(1, i, w.trees[i].base, w.trees[i].base + V3(0, w.trees[i].height * 0.4, 0), "TREE");
    for (int i = 0; i < (int)w.logs.size(); i++) take(2, i, (w.logs[i].a + w.logs[i].b) * 0.5, (w.logs[i].a + w.logs[i].b) * 0.5, "LOG");
    for (int i = 0; i < (int)w.boxes.size(); i++)
        if (!w.boxes[i].hidden && (everything || w.boxes[i].rock || ownerOf(3, i) >= 0)) take(3, i, w.boxes[i].c, w.boxes[i].c, w.boxes[i].rock ? "ROCK SLAB" : "BLOCK");
    for (int i = 0; i < (int)w.fixtures.size(); i++) {
        const Prop& f = w.fixtures[i];
        if (!everything && ownerOf(4, i) < 0) continue;
        take(4, i, f.a, f.kind == PROP_FENCE ? (f.a + f.b) * 0.5 + V3(0, 0.6, 0) : f.a + V3(0, f.kind == PROP_GARAGE ? 2 : 1, 0), f.kind == PROP_GARAGE ? "REPAIR SHOP" : f.kind == PROP_FENCE ? "FENCE" : "PROP");
    }
    for (int i = 0; i < (int)w.bushes.size(); i++) take(6, i, w.bushes[i].p, w.bushes[i].p + V3(0, w.bushes[i].r * plantInfo(w.bushes[i].kind).cy * 0.9, 0), plantInfo(w.bushes[i].kind).name);
    for (int i = 0; i < (int)w.models.size(); i++)
        if (ownerOf(5, i) >= 0) take(5, i, w.models[i].p, w.models[i].p + V3(0, 1, 0), "STRUCTURE");
    return found;
}

EditPick EditCore::pick(const V3& o, const V3& d, double hitT, bool includeBase, bool course) const {
    EditPick best;
    best.t = hitT + 0.5;  // (things standing on the ground count up to where the ray meets it)
    if (!world) return best;
    const World& w = *world;
    auto consider = [&](double t, EditPick::Kind kind, int index, int baseKind, const V3& at, const std::string& name) {
        if (t >= best.t) return;
        if (kind == EditPick::BASE) {
            const int ow = ownerOf(baseKind, index);
            if (ow >= 0) {
                kind = EditPick::OBJECT;
                index = ow;
                baseKind = -1;
            } else if (!includeBase) {
                return;
            }
        }
        best.kind = kind, best.index = index, best.baseKind = baseKind, best.at = at, best.t = t, best.name = name;
    };
    double t;
    // (Each is tested as its real shape: a ray past a rock's corner but through its bounding sphere is a miss.)
    for (int i = 0; i < (int)w.rocks.size(); i++) {
        const Rock& r = w.rocks[i];
        if (objshape::Hit h; objshape::rayRock(r, o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 0, r.c, "ROCK");
    }
    for (int i = 0; i < (int)w.trees.size(); i++) {
        const Tree& tr = w.trees[i];
        if (rayTree(o, d, tr, t)) consider(t, EditPick::BASE, i, 1, tr.base, "TREE");
    }
    for (int i = 0; i < (int)w.logs.size(); i++) {
        const Log& g = w.logs[i];
        if (objshape::Hit h; objshape::rayLog(g, o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 2, (g.a + g.b) * 0.5, "LOG");
    }
    for (int i = 0; i < (int)w.boxes.size(); i++) {
        const Box& b = w.boxes[i];
        if (b.hidden) continue;
        if (objshape::Hit h; objshape::rayBox(b, o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 3, b.c, b.rock ? "ROCK SLAB" : "BLOCK");
    }
    for (int i = 0; i < (int)w.fixtures.size(); i++) {
        const Prop& f = w.fixtures[i];
        static const char* names[] = {"FENCE", "CABIN", "SIGN", "TRAIL MAP", "TABLE", "STUMP", "REPAIR SHOP", "MARKER", "CAIRN"};
        const std::string name = names[std::min((int)f.kind, 8)];
        Placed pl;
        int sub;
        if (f.kind == PROP_FENCE) {
            if (objshape::Hit h; objshape::rayBox(fenceBox(f), o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 4, f.a, name);
        } else if (f.kind == PROP_GARAGE) {
            if (objshape::Hit h; objshape::rayBox(shedBox(f), o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 4, f.a, name);
        } else if (propAsModel(f, i, pl, sub)) {
            if (objshape::Hit h; objshape::rayBox(modelBox(pl), o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 4, f.a, name);
        } else if (raySegment(o, d, f.a, f.a + V3(0, 1.6, 0), t) < 0.3) {
            consider(t, EditPick::BASE, i, 4, f.a, name);
        }
    }
    for (int i = 0; i < (int)w.bushes.size(); i++) {
        const Bush& b = w.bushes[i];
        const PlantInfo& pi = plantInfo(b.kind);
        if (rayEllipsoid(o, d, b.p + V3(0, b.r * pi.cy, 0), b.r * pi.rx, b.r * pi.ry, t)) consider(t, EditPick::BASE, i, 6, b.p, pi.name);
    }
    for (int i = 0; i < (int)w.models.size(); i++) {
        if (ownerOf(5, i) < 0) continue;  // (only the structures you put there)
        if (objshape::Hit h; objshape::rayBox(modelBox(w.models[i]), o, d, best.t, h)) consider(h.t, EditPick::BASE, i, 5, w.models[i].p, "STRUCTURE");
    }
    for (int k = 0; course && k < (int)level.gates.size(); k++) {
        const Gate& g = level.gates[k];
        const V3 f(std::cos(g.yaw), 0, -std::sin(g.yaw)), l(-f.z, 0, f.x);
        const V3 pa = g.p + l * (g.width / 2), pb = g.p - l * (g.width / 2);
        const V3 a(pa.x, w.height(pa.x, pa.z), pa.z), b(pb.x, w.height(pb.x, pb.z), pb.z);
        const double top = std::max(a.y, b.y) + 4.5;
        double tt = 1e18;
        if (raySegment(o, d, a, V3(a.x, top, a.z), t) < 0.7) tt = std::min(tt, t);
        if (raySegment(o, d, b, V3(b.x, top, b.z), t) < 0.7) tt = std::min(tt, t);
        if (raySegment(o, d, V3(a.x, top - 0.5, a.z), V3(b.x, top - 0.5, b.z), t) < 0.8) tt = std::min(tt, t);
        if (raySegment(o, d, V3(g.p.x, w.height(g.p.x, g.p.z) + 0.3, g.p.z), V3(g.p.x, w.height(g.p.x, g.p.z) + 0.3, g.p.z) + f * 3, t) < 1.2) tt = std::min(tt, t);
        if (tt < 1e17) consider(tt, EditPick::GATE, k, -1, g.p, "GATE " + std::to_string(k + 1));
    }
    if (course && level.hasStart && raySphere(o, d, V3(level.start.x, w.height(level.start.x, level.start.z) + 0.8, level.start.z), 2.6, t))
        consider(t, EditPick::START, 0, -1, level.start, "START");
    if (best.kind == EditPick::OBJECT && best.index >= 0 && best.index < (int)level.objects.size()) {
        best.at = level.objects[best.index].p;
        best.name = kObjectNames[std::min(std::max(level.objects[best.index].kind, 0), OBJ_KIND_COUNT - 1)];
    }
    return best;
}
