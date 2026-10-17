#include "editcore.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>

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

bool rayBox(const V3& o, const V3& d, const Box& b, double& t) {
    double t0 = 0, t1 = 1e18;
    const V3 p = b.c - o;
    const double h[3] = {b.h.x, b.h.y, b.h.z};
    for (int k = 0; k < 3; k++) {
        const double e = dot(b.ax[k], p), f = dot(b.ax[k], d);
        if (std::fabs(f) > 1e-12) {
            double a = (e + h[k]) / f, c = (e - h[k]) / f;
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a), t1 = std::min(t1, c);
            if (t0 > t1) return false;
        } else if (-e - h[k] > 0 || -e + h[k] < 0) {
            return false;
        }
    }
    t = t0;
    return true;
}

bool sameObject(const LevelObject& a, const LevelObject& b) {
    auto eq = [](const V3& p, const V3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.kind == b.kind && eq(a.p, b.p) && eq(a.size, b.size) && eq(a.p2, b.p2) && a.yaw == b.yaw && a.pitch == b.pitch &&
           a.roll == b.roll && a.top == b.top && a.seed == b.seed && a.sub == b.sub && a.col[0] == b.col[0] && a.col[1] == b.col[1] &&
           a.col[2] == b.col[2] && a.name == b.name;
}

bool sameObjects(const Level& a, const Level& b) {
    if (a.objects.size() != b.objects.size() || a.removed.size() != b.removed.size()) return false;
    for (size_t k = 0; k < a.objects.size(); k++)
        if (!sameObject(a.objects[k], b.objects[k])) return false;
    for (size_t k = 0; k < a.removed.size(); k++)
        if (a.removed[k].x != b.removed[k].x || a.removed[k].z != b.removed[k].z) return false;
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

void EditCore::begin(World& w, const Level& lv) {
    world = &w;
    base.rocks = w.rocks, base.trees = w.trees, base.poles = w.poles, base.logs = w.logs;
    base.boxes = w.boxes, base.fixtures = w.fixtures, base.stations = w.stations;
    base.spawn = w.spawn, base.spawnYaw = w.spawnYaw;
    level = lv;
    level.map = w.map;
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
    rebuildObjects();
    if (level.section.on) clearBushes();
    syncSpawn();
    history.clear();
    future.clear();
    pending = Entry();
    pendingCells.clear();
    depth = 0;
    changes = savedChanges = 0;
    dirtyCells.add(0, 0);
    dirtyCells.add(n - 1, n - 1);
    dirtyWater = dirtyObjects = true;
}

void EditCore::rebuildObjects() {
    World& w = *world;
    w.rocks = base.rocks, w.trees = base.trees, w.poles = base.poles, w.logs = base.logs;
    w.boxes = base.boxes, w.fixtures = base.fixtures, w.stations = base.stations;
    for (const V3& r : level.removed) w.removeNear(r.x, r.z, 1.5);
    baseCount[0] = (int)w.rocks.size(), baseCount[1] = (int)w.trees.size(), baseCount[2] = (int)w.logs.size();
    baseCount[3] = (int)w.boxes.size(), baseCount[4] = (int)w.fixtures.size();
    for (int k = 0; k < 5; k++) owner[k].assign(baseCount[k], -1);
    for (int k = 0; k < (int)level.objects.size(); k++) {
        const LevelObject& o = level.objects[k];
        const size_t sz[5] = {w.rocks.size(), w.trees.size(), w.logs.size(), w.boxes.size(), w.fixtures.size()};
        placeObject(o, w);
        const size_t now[5] = {w.rocks.size(), w.trees.size(), w.logs.size(), w.boxes.size(), w.fixtures.size()};
        for (int q = 0; q < 5; q++)
            for (size_t i = sz[q]; i < now[q]; i++) owner[q].push_back(k);
    }
    w.buildGrid();
    dirtyObjects = true;
}

int EditCore::ownerOf(int baseKind, int index) const {
    if (baseKind < 0 || baseKind >= 5 || index < 0 || index >= (int)owner[baseKind].size()) return -1;
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
        const double half = World::kHalfSize;
        for (const LevelObject& o : level.objects) {
            const double i = o.p.x + half, j = o.p.z + half, m = std::max(3.0, std::max(o.size.x, o.size.z) + 2);
            if (!r.empty() && i + m >= r.i0 && i - m <= r.i1 && j + m >= r.j0 && j - m <= r.j1) hit = true;
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
    const double half = World::kHalfSize, r = std::max(b.radius, 0.5);
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
    const double half = World::kHalfSize, hw = width / 2, shoulder = std::max(2.0, width * 0.5);
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
    const double half = World::kHalfSize, R = std::hypot(hl, hw) + margin;
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
    const double half = World::kHalfSize, r = std::max(b.radius, 0.5);
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
    const double half = World::kHalfSize;
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
    const double half = World::kHalfSize, r = std::max(b.radius, 0.5);
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
    const double half = World::kHalfSize;
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
    const World& w = *world;
    switch (o.kind) {
        case OBJ_BOX: {
            V3 ax[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
            for (V3& a : ax) {
                a = rotateAxis(a, V3(1, 0, 0), o.roll);
                a = rotateAxis(a, V3(0, 0, 1), o.pitch);
                a = rotateAxis(a, V3(0, 1, 0), o.yaw);
            }
            if (std::fabs(o.pitch) < 1e-3 && std::fabs(o.roll) < 1e-3) {
                // Flat: down to the lowest ground under it, so no corner hangs in the air.
                double g = w.height(o.p.x, o.p.z);
                for (int a = -1; a <= 1; a += 2)
                    for (int b = -1; b <= 1; b += 2) {
                        const V3 c = o.p + ax[0] * (a * o.size.x) + ax[2] * (b * o.size.z);
                        g = std::min(g, w.height(c.x, c.z));
                    }
                o.p.y = g + o.size.y;
            } else {
                // Tipped (a ramp): the low end's top edge meets the ground.
                const double s = o.pitch >= 0 ? -1 : 1;
                const V3 off = ax[0] * (s * o.size.x) + ax[1] * o.size.y;
                o.p.y = w.height(o.p.x + off.x, o.p.z + off.z) - off.y;
            }
            break;
        }
        case OBJ_LOG:
            o.p.y = w.height(o.p.x, o.p.z) + o.size.x;
            o.p2.y = w.height(o.p2.x, o.p2.z) + o.size.x;
            break;
        default: o.p.y = w.height(o.p.x, o.p.z); break;
    }
    return o;
}

static const char* kObjectNames[OBJ_KIND_COUNT] = {"ROCK", "BLOCK", "LOG", "TREE", "REPAIR SHOP"};

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
    level.removed.push_back(V3(p.at.x, 0, p.at.z));
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
    level.removed.push_back(V3(f.a.x, 0, f.a.z));
    level.objects.push_back(o);
    rebuildObjects();
    endEdit();
    return (int)level.objects.size() - 1;
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
                                           const V3 at = o.kind == OBJ_LOG ? (o.p + o.p2) * 0.5 : o.p;
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

EditPick EditCore::pick(const V3& o, const V3& d, double hitT, bool includeBase) const {
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
    for (int i = 0; i < (int)w.rocks.size(); i++) {
        const Rock& r = w.rocks[i];
        if (raySphere(o, d, r.c, r.r * 0.85, t)) consider(t, EditPick::BASE, i, 0, r.c, "ROCK");
    }
    for (int i = 0; i < (int)w.trees.size(); i++) {
        const Tree& tr = w.trees[i];
        const double dist = raySegment(o, d, tr.base, tr.base + V3(0, tr.height * 0.9, 0), t);
        if (dist < std::max(0.6, tr.radius * 0.55)) consider(t, EditPick::BASE, i, 1, tr.base, "TREE");
    }
    for (int i = 0; i < (int)w.logs.size(); i++) {
        const Log& g = w.logs[i];
        if (raySegment(o, d, g.a, g.b, t) < g.r + 0.2) consider(t, EditPick::BASE, i, 2, (g.a + g.b) * 0.5, "LOG");
    }
    for (int i = 0; i < (int)w.boxes.size(); i++) {
        const Box& b = w.boxes[i];
        if (b.hidden) continue;
        if (rayBox(o, d, b, t)) consider(t, EditPick::BASE, i, 3, b.c, b.rock ? "ROCK SLAB" : "BLOCK");
    }
    for (int i = 0; i < (int)w.fixtures.size(); i++) {
        const Prop& f = w.fixtures[i];
        static const char* names[] = {"FENCE", "CABIN", "SIGN", "TRAIL MAP", "TABLE", "STUMP", "REPAIR SHOP", "MARKER", "CAIRN"};
        const std::string name = names[std::min((int)f.kind, 8)];
        if (f.kind == PROP_FENCE) {
            if (raySegment(o, d, f.a + V3(0, 0.6, 0), f.b + V3(0, 0.6, 0), t) < 0.8) consider(t, EditPick::BASE, i, 4, f.a, name);
        } else if (f.kind == PROP_GARAGE) {
            // (A map's own repair shop goes as a whole: the station, the shed and its posts.)
            if (raySphere(o, d, f.a + V3(0, 2.5, 0), 5.5, t)) consider(t, EditPick::BASE, i, 4, f.a, name);
        } else {
            const double r = f.kind == PROP_CABIN ? 4.0 : 1.4;
            if (raySphere(o, d, f.a + V3(0, r * 0.6, 0), r, t)) consider(t, EditPick::BASE, i, 4, f.a, name);
        }
    }
    for (int k = 0; k < (int)level.gates.size(); k++) {
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
    if (level.hasStart && raySphere(o, d, V3(level.start.x, w.height(level.start.x, level.start.z) + 0.8, level.start.z), 2.6, t))
        consider(t, EditPick::START, 0, -1, level.start, "START");
    if (best.kind == EditPick::OBJECT && best.index >= 0 && best.index < (int)level.objects.size()) {
        best.at = level.objects[best.index].p;
        best.name = kObjectNames[std::min(std::max(level.objects[best.index].kind, 0), OBJ_KIND_COUNT - 1)];
    }
    return best;
}
