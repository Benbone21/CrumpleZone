// What the autopilot sees (see pilotsense.h).
#include "pilotsense.h"

#include <cmath>

#include "softbody.h"

const char* seenName(int kind) {
    switch (kind) {
        case SEEN_ROCK: return "ROCK";
        case SEEN_LEDGE: return "LEDGE";
        case SEEN_LOG: return "LOG";
        case SEEN_TREE: return "TREE";
        case SEEN_VEHICLE: return "VEHICLE";
        case SEEN_WALL: return "WALL";
        default: return "WATER";
    }
}

// ---- Exact tops ---------------------------------------------------------------------

double rockTop(const Rock& r, double x, double z) {
    if (!r.parts.empty()) {
        double best = -1e9;
        for (const Rock& p : r.parts) best = std::max(best, rockTop(p, x, z));
        return best;
    }
    // Inside the hull: dot(n, p) <= d for every face. Along a vertical line that bounds y
    // from above (faces turned up) and below (faces turned down).
    double hi = 1e9, lo = -1e9;
    for (size_t f = 0; f < r.fn.size(); f++) {
        const V3& n = r.fn[f];
        const double rest = r.fd[f] - n.x * x - n.z * z;
        if (n.y > 1e-6) hi = std::min(hi, rest / n.y);
        else if (n.y < -1e-6) lo = std::max(lo, rest / n.y);
        else if (rest < 0) return -1e9;
        if (hi < lo) return -1e9;
    }
    return hi > 1e8 ? -1e9 : hi;
}

double boxTop(const Box& b, double x, double z) {
    double hi = 1e9, lo = -1e9;
    for (int k = 0; k < 3; k++) {
        const V3& a = b.ax[k];
        const double h = k == 0 ? b.h.x : k == 1 ? b.h.y : b.h.z;
        const double A = (x - b.c.x) * a.x - b.c.y * a.y + (z - b.c.z) * a.z;  // dot(p - c, a) at y = 0
        if (std::fabs(a.y) < 1e-9) {
            if (std::fabs(A) > h) return -1e9;
            continue;
        }
        double y0 = (-h - A) / a.y, y1 = (h - A) / a.y;
        if (y0 > y1) std::swap(y0, y1);
        lo = std::max(lo, y0);
        hi = std::min(hi, y1);
        if (hi < lo) return -1e9;
    }
    return hi > 1e8 ? -1e9 : hi;
}

double logTop(const Log& l, double x, double z) {
    const double dx = l.b.x - l.a.x, dz = l.b.z - l.a.z;
    const double L2 = dx * dx + dz * dz;
    const double t = L2 > 1e-9 ? clampd(((x - l.a.x) * dx + (z - l.a.z) * dz) / L2, 0, 1) : 0;
    const double px = l.a.x + dx * t, pz = l.a.z + dz * t;
    const double d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
    if (d2 >= l.r * l.r) return -1e9;
    return l.a.y + (l.b.y - l.a.y) * t + std::sqrt(l.r * l.r - d2);
}

// ---- The scene around the vehicle -----------------------------------------------------

void Scene::gather(const World& world, const std::vector<Car>* cs, const Car* self, const V3& around, double radius) {
    w = &world;
    cars = cs;
    list.clear();
    origin = around;
    h.resize(kN * kN);
    id.resize(kN * kN);
    have.assign(kN * kN, 0);
    auto near = [&](double x, double z, double r) { return std::hypot(x - around.x, z - around.z) < radius + r; };
    World::NearbySolids nearby;
    w->solidsNear(around.x - radius, around.z - radius, around.x + radius, around.z + radius, nearby);
    // Keep the original index order so equal-cost choices stay deterministic.
    for (auto* indices : {&nearby.rocks, &nearby.boxes, &nearby.logs, &nearby.poles})
        std::sort(indices->begin(), indices->end());
    for (int i : nearby.rocks) {
        const Rock& r = w->rocks[i];
        if (!near(r.c.x, r.c.z, r.r)) continue;
        double top = -1e9;
        for (const V3& v : r.v) top = std::max(top, v.y);
        const double hgt = top - w->height(r.c.x, r.c.z);
        if (hgt < 0.05) continue;
        double reach = 0;
        for (const V3& v : r.v) reach = std::max(reach, std::hypot(v.x - r.c.x, v.z - r.c.z));
        list.push_back({SEEN_ROCK, i, r.c.x, r.c.z, reach, hgt});
    }
    for (int i : nearby.boxes) {
        const Box& b = w->boxes[i];
        if (!b.solid && !b.hidden) continue;
        const double reach = std::hypot(b.h.x, b.h.z) + b.h.y;
        if (!near(b.c.x, b.c.z, reach)) continue;
        double hgt = 0;
        for (int sx = -1; sx <= 1; sx++)
            for (int sz = -1; sz <= 1; sz++) {
                const V3 p = b.c + b.ax[0] * (b.h.x * 0.8 * sx) + b.ax[2] * (b.h.z * 0.8 * sz);
                const double t = boxTop(b, p.x, p.z);
                if (t > -1e8) hgt = std::max(hgt, t - w->height(p.x, p.z));
            }
        list.push_back({(uint8_t)(hgt > 1.2 ? SEEN_WALL : SEEN_LEDGE), i, b.c.x, b.c.z, reach, hgt});
    }
    for (int i : nearby.logs) {
        const Log& l = w->logs[i];
        const V3 m = (l.a + l.b) * 0.5;
        const double reach = len(l.b - l.a) * 0.5 + l.r;
        if (!near(m.x, m.z, reach)) continue;
        list.push_back({SEEN_LOG, i, m.x, m.z, reach, m.y + l.r - w->height(m.x, m.z)});
    }
    for (int i : nearby.poles) {
        const Pole& p = w->poles[i];
        if (!near(p.x, p.z, p.r)) continue;
        list.push_back({SEEN_TREE, i, p.x, p.z, p.r, p.y1 - w->height(p.x, p.z)});
    }
    if (cs)
        for (int i = 0; i < (int)cs->size(); i++) {
            const Car& c = (*cs)[i];
            if (&c == self || c.nodes.empty()) continue;
            const Frame fr = c.frame();
            const double reach = std::hypot(c.spec->length, c.spec->width) * 0.5;
            if (!near(fr.com.x, fr.com.z, reach)) continue;
            // Its footprint as it stands: every node, bumpers, mirrors and tires. (A box of the
            // spec's length and width round its center of mass sat ~0.4 m forward of a 4Runner's
            // tail and inside its mirrors, and the line round a parked one clipped its corner.)
            const V3 f = norm(V3(fr.fwd.x, 0, fr.fwd.z));
            double a0 = 1e9, a1 = -1e9, s0 = 1e9, s1 = -1e9;
            for (const Node& n : c.nodes) {
                const double dx = n.p.x - fr.com.x, dz = n.p.z - fr.com.z;
                const double along = dx * f.x + dz * f.z, across = dx * f.z - dz * f.x;
                a0 = std::min(a0, along), a1 = std::max(a1, along), s0 = std::min(s0, across), s1 = std::max(s1, across);
            }
            const double am = 0.5 * (a0 + a1), sm = 0.5 * (s0 + s1);
            Thing t{SEEN_VEHICLE, i, fr.com.x + f.x * am + f.z * sm, fr.com.z + f.z * am - f.x * sm, reach, 1.5};
            t.fx = f.x, t.fz = f.z, t.hl = 0.5 * (a1 - a0), t.hw = 0.5 * (s1 - s0);
            t.r = std::hypot(t.hl, t.hw);
            list.push_back(t);
        }
    for (auto& b : bins) b.clear();
    farList.clear();
    const double half = kBin * kBins / 2;
    for (int k = 0; k < (int)list.size(); k++) {
        const Thing& t = list[k];
        farList.push_back(k);
        const int i0 = std::max(0, (int)std::floor((t.x - t.r - origin.x + half) / kBin)), i1 = std::min(kBins - 1, (int)std::floor((t.x + t.r - origin.x + half) / kBin));
        const int j0 = std::max(0, (int)std::floor((t.z - t.r - origin.z + half) / kBin)), j1 = std::min(kBins - 1, (int)std::floor((t.z + t.r - origin.z + half) / kBin));
        for (int i = i0; i <= i1; i++)
            for (int j = j0; j <= j1; j++) bins[i * kBins + j].push_back(k);
    }
}

double Scene::grip(double x, double z) const { return surfaceGrip(w->surfaceAt(x, z)); }

double Scene::ground(double x, double z) const { return w->height(x, z); }

double Scene::compute(double x, double z, int* what) const {
    const double g = w->height(x, z);
    double best = g;
    int who = -1;
    if (w->waterLevel(x, z) - g > 0.45) best = g + 5, who = -2;  // deep water: no way through
    const double half = kBin * kBins / 2;
    const int bi = (int)std::floor((x - origin.x + half) / kBin), bj = (int)std::floor((z - origin.z + half) / kBin);
    const std::vector<int>& cand = bi >= 0 && bj >= 0 && bi < kBins && bj < kBins ? bins[bi * kBins + bj] : farList;
    for (int k : cand) {
        const Thing& t = list[k];
        if ((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) > t.r * t.r) continue;
        double top = -1e9;
        switch (t.kind) {
            case SEEN_ROCK: top = rockTop(w->rocks[t.index], x, z); break;
            case SEEN_LEDGE:
            case SEEN_WALL: top = boxTop(w->boxes[t.index], x, z); break;
            case SEEN_LOG: top = logTop(w->logs[t.index], x, z); break;
            case SEEN_TREE: top = w->poles[t.index].y1; break;
            case SEEN_VEHICLE: {
                const double dx = x - t.x, dz = z - t.z;
                const double along = dx * t.fx + dz * t.fz, across = dx * t.fz - dz * t.fx;
                if (std::fabs(along) < t.hl && std::fabs(across) < t.hw) top = g + 1.5;
                break;
            }
            default: break;
        }
        if (top > best) best = top, who = k;
    }
    if (what) *what = who;
    return best;
}

double Scene::top(double x, double z, int* what) {
    const int i = (int)std::floor((x - origin.x) / kRes) + kN / 2, j = (int)std::floor((z - origin.z) / kRes) + kN / 2;
    if (i < 0 || j < 0 || i >= kN || j >= kN) return compute(x, z, what);
    const int c = i * kN + j;
    if (!have[c]) {
        int who;
        // Sample at the cell's center so every query in it agrees.
        h[c] = (float)compute(origin.x + (i - kN / 2 + 0.5) * kRes, origin.z + (j - kN / 2 + 0.5) * kRes, &who);
        id[c] = (int16_t)std::max(-2, std::min(who, 32767));
        have[c] = 1;
    }
    if (what) *what = id[c];
    return h[c];
}

// ---- Drive setups ---------------------------------------------------------------------

int modeForLevel(const VehicleSpec& vs, int level) {
    const auto& modes = driveModes(vs.transfer);
    for (int m = 0; m < (int)modes.size(); m++) {
        const DriveMode& d = modes[m];
        const int l = d.low ? LEVEL_LOW : !d.front ? LEVEL_2WD : d.centerLock ? LEVEL_4WD_LOCKED : LEVEL_4WD;
        if (l == level) return m;
    }
    return -1;
}

int currentLevel(const Car& c) {
    const DriveMode* m = c.mode();
    if (!m) return LEVEL_2WD;
    return m->low ? LEVEL_LOW : !m->front ? LEVEL_2WD : m->centerLock ? LEVEL_4WD_LOCKED : LEVEL_4WD;
}

int bestLevel(const VehicleSpec& vs) {
    for (int l = LEVEL_COUNT - 1; l > 0; l--)
        if (modeForLevel(vs, l) >= 0) return l;
    return LEVEL_2WD;
}

const char* levelName(const Car& c, int level) {
    const int m = modeForLevel(*c.spec, level);
    if (m < 0) return level == LEVEL_2WD ? "2WD" : "?";
    return driveModes(c.spec->transfer)[m].name;
}

double stepCap(const VehicleSpec& vs, int level, int lockers, double psi) {
    // Measured on square ledges (tests: Bronco 33s climb 0.15 m in 2H, 0.30 in 4H, 0.45
    // in 4L; the 4Runner 0.15 / 0.20 / 0.35): roughly these shares of the tire radius.
    static const double k[LEVEL_COUNT] = {0.38, 0.5, 0.62, 0.95};
    double share = k[std::min(std::max(level, 0), LEVEL_COUNT - 1)] + 0.05 * lockers + 0.006 * std::max(0.0, 35 - psi);
    return share * vs.wheelRadius * vs.ledgeFactor;  // (the buggy's 40s on 16 in of travel clear ~0.7 m; the side-by-side's 32s ~0.3 m: see tests/offroad.cpp)
}
