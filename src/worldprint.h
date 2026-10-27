#pragma once
// A fingerprint of a built world, by category: every placed thing (its kind, position to 1 mm, its turn and size) and the land (each
// cell's height to 1 mm, its surface and paint, its water). For checking that two machines (Linux, Windows, Mac; any thread count)
// build the same world: tools/worldprint prints it, tests/onemap.cpp's onemap/xplat-fingerprint compares it with Linux's.
//
// `dump` (optional): a line per object (full precision) and per 256 m tile of land, in build order, so two dumps can be diffed to
// find the first thing that differs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "world.h"

namespace worldprint {

enum Cat { TERRAIN, GROUND, WATER, ROCKS, TREES, LOGS, BUSHES, BOXES, POLES, PROPS, PLACES, TRAILS, CAT_COUNT };
inline const char* catName(int c) {
    static const char* n[CAT_COUNT] = {"terrain", "ground", "water", "rocks", "trees", "logs", "bushes", "boxes", "poles", "props", "places", "trails"};
    return n[c];
}

struct Print {
    uint64_t cat[CAT_COUNT];
    size_t count[CAT_COUNT];
    uint64_t all() const {
        uint64_t h = 1469598103934665603ull;
        for (int c = 0; c < CAT_COUNT; c++) h = (h ^ cat[c]) * 1099511628211ull;
        return h;
    }
};

namespace detail {
struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void u(uint64_t v) {
        for (int i = 0; i < 8; i++) h = (h ^ ((v >> (8 * i)) & 0xff)) * 1099511628211ull;
    }
    void mm(double v) { u((uint64_t)(int64_t)std::llround(v * 1000.0)); }  // to 1 mm (and 1/1000 of anything else)
    void p(const V3& v) { mm(v.x), mm(v.y), mm(v.z); }
    void s(const std::string& t) {
        u(t.size());
        for (char c : t) u((uint8_t)c);
    }
};
}  // namespace detail

inline Print print(const World& w, FILE* dump = nullptr) {
    using detail::Fnv;
    Fnv f[CAT_COUNT];
    Print out{};
    // (Doubles go in the dump as their bits: two printfs may not print the same digits for the same value.)
    auto H = [](double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof u);
        char b[20];
        std::snprintf(b, sizeof b, "%016llx", (unsigned long long)u);
        return std::string(b);
    };
    auto D = [&](const char* fmt, auto... a) {
        if (dump) std::fprintf(dump, fmt, a...);
    };
    // The land, a 256 m tile at a time in the dump.
    const int n = w.gridSize();
    const int tile = 256;
    for (int ti = 0; ti < n; ti += tile)
        for (int tj = 0; tj < n; tj += tile) {
            Fnv th, tg, tw;
            for (int i = ti; i < std::min(n, ti + tile); i++)
                for (int j = tj; j < std::min(n, tj + tile); j++) {
                    const int id = i * n + j;
                    const V3 c = w.cellCenter(id);
                    th.mm(w.cellHeight(id));
                    tg.u(w.cellSurface(id)), tg.u(w.cellPaint(id));
                    const double wl = w.waterLevel(c.x, c.z);
                    tw.mm(wl < -1e8 ? -1e6 : wl);
                    const V3 fl = w.waterFlow(c.x, c.z);
                    tw.mm(fl.x), tw.mm(fl.z);
                }
            f[TERRAIN].u(th.h), f[GROUND].u(tg.h), f[WATER].u(tw.h);
            D("tile %d %d terrain %016llx ground %016llx water %016llx\n", ti, tj, (unsigned long long)th.h, (unsigned long long)tg.h,
              (unsigned long long)tw.h);
        }
    out.count[TERRAIN] = out.count[GROUND] = (size_t)n * n;
    for (const Water& wt : w.waters) {
        f[WATER].mm(wt.x), f[WATER].mm(wt.z), f[WATER].mm(wt.r), f[WATER].mm(wt.y);
        D("lake %s %s %s %s\n", H(wt.x).c_str(), H(wt.z).c_str(), H(wt.r).c_str(), H(wt.y).c_str());
    }
    for (const River& r : w.rivers) {
        f[WATER].s(r.name);
        for (const RiverSpot& s : r.spots) {
            f[WATER].p(s.p), f[WATER].mm(s.depth), f[WATER].mm(s.halfWidth), f[WATER].mm(s.speed), f[WATER].u(s.cls), f[WATER].u(s.bed);
            D("river %s %s %s %s %s %s\n", r.name.c_str(), H(s.p.x).c_str(), H(s.p.y).c_str(), H(s.p.z).c_str(), H(s.depth).c_str(), H(s.halfWidth).c_str());
        }
    }
    for (const Falls& fa : w.falls) {
        f[WATER].s(fa.name), f[WATER].p(fa.lip), f[WATER].mm(fa.yaw), f[WATER].mm(fa.height), f[WATER].mm(fa.width), f[WATER].u(fa.seed);
        D("falls %s %s %s %s %s\n", fa.name.c_str(), H(fa.lip.x).c_str(), H(fa.lip.y).c_str(), H(fa.lip.z).c_str(), H(fa.yaw).c_str());
    }
    out.count[WATER] = w.waters.size() + w.rivers.size() + w.falls.size();

    for (const Rock& k : w.rocks) {
        Fnv& h = f[ROCKS];
        h.p(k.c), h.mm(k.r), h.mm(k.tint), h.u(k.seed), h.u((uint64_t)(int64_t)k.scan), h.u(k.v.size()), h.u(k.parts.size());
        if (k.scan >= 0) h.p(k.origin), h.p(k.axis[0]), h.p(k.axis[1]), h.p(k.axis[2]);
        for (const V3& v : k.v) h.p(v);
        for (const Rock& pt : k.parts) h.p(pt.c), h.mm(pt.r), h.u(pt.v.size());
        D("rock %s %s %s r %s seed %u scan %d v %zu parts %zu ax %s %s %s\n", H(k.c.x).c_str(), H(k.c.y).c_str(), H(k.c.z).c_str(), H(k.r).c_str(), k.seed, k.scan, k.v.size(), k.parts.size(), H(k.axis[0].x).c_str(), H(k.axis[0].y).c_str(), H(k.axis[0].z).c_str());
    }
    for (const Tree& t : w.trees) {
        f[TREES].p(t.base), f[TREES].mm(t.height), f[TREES].mm(t.radius), f[TREES].mm(t.tint), f[TREES].u(t.kind), f[TREES].u((uint64_t)(int64_t)t.design);
        D("tree %s %s %s h %s r %s kind %d design %d\n", H(t.base.x).c_str(), H(t.base.y).c_str(), H(t.base.z).c_str(), H(t.height).c_str(), H(t.radius).c_str(), t.kind, t.design);
    }
    for (const Log& l : w.logs) {
        f[LOGS].p(l.a), f[LOGS].p(l.b), f[LOGS].mm(l.r);
        D("log %s %s %s  %s %s %s r %s\n", H(l.a.x).c_str(), H(l.a.y).c_str(), H(l.a.z).c_str(), H(l.b.x).c_str(), H(l.b.y).c_str(), H(l.b.z).c_str(), H(l.r).c_str());
    }
    for (const Bush& b : w.bushes) {
        f[BUSHES].p(b.p), f[BUSHES].mm(b.r), f[BUSHES].mm(b.tint), f[BUSHES].u((uint64_t)(int64_t)b.kind);
        D("bush %s %s %s r %s kind %d\n", H(b.p.x).c_str(), H(b.p.y).c_str(), H(b.p.z).c_str(), H(b.r).c_str(), b.kind);
    }
    for (const Box& b : w.boxes) {
        Fnv& h = f[BOXES];
        h.p(b.c), h.p(b.h), h.p(b.ax[0]), h.p(b.ax[1]), h.p(b.ax[2]), h.u(b.hidden), h.u(b.solid), h.u(b.rock);
        D("box %s %s %s h %s %s %s ax %s %s %s\n", H(b.c.x).c_str(), H(b.c.y).c_str(), H(b.c.z).c_str(), H(b.h.x).c_str(), H(b.h.y).c_str(), H(b.h.z).c_str(), H(b.ax[0].x).c_str(), H(b.ax[0].y).c_str(), H(b.ax[0].z).c_str());
    }
    for (const Pole& p : w.poles) {
        f[POLES].mm(p.x), f[POLES].mm(p.z), f[POLES].mm(p.y0), f[POLES].mm(p.y1), f[POLES].mm(p.r);
        D("pole %s %s %s %s %s\n", H(p.x).c_str(), H(p.z).c_str(), H(p.y0).c_str(), H(p.y1).c_str(), H(p.r).c_str());
    }
    for (const Prop& p : w.fixtures) {
        f[PROPS].u(p.kind), f[PROPS].p(p.a), f[PROPS].p(p.b), f[PROPS].mm(p.yaw);
        D("fixture %d %s %s %s yaw %s\n", (int)p.kind, H(p.a.x).c_str(), H(p.a.y).c_str(), H(p.a.z).c_str(), H(p.yaw).c_str());
    }
    for (const Placed& m : w.models) {
        f[PROPS].s(m.id), f[PROPS].p(m.p), f[PROPS].mm(m.yaw), f[PROPS].p(m.scale), f[PROPS].u((uint64_t)(int64_t)m.variant);
        for (const auto& kv : m.set) f[PROPS].s(kv.first), f[PROPS].mm(kv.second);
        D("model %s %s %s %s yaw %s\n", m.id.c_str(), H(m.p.x).c_str(), H(m.p.y).c_str(), H(m.p.z).c_str(), H(m.yaw).c_str());
    }
    out.count[PROPS] = w.fixtures.size() + w.models.size();
    {
        Fnv& h = f[PLACES];
        h.p(w.spawn), h.mm(w.spawnYaw);
        for (const Station& s : w.stations) h.p(s.p), h.mm(s.yaw), h.s(s.name), D("station %s %s %s %s\n", s.name, H(s.p.x).c_str(), H(s.p.y).c_str(), H(s.p.z).c_str());
        for (const SpawnPad& s : w.spawnPads) h.p(s.p), h.mm(s.yaw), h.mm(s.back), h.mm(s.front), h.mm(s.half), h.u(s.slots);
        for (const World::Tag& t : w.tags) h.s(t.text), h.p(t.p), D("tag %s %s %s %s\n", t.text.c_str(), H(t.p.x).c_str(), H(t.p.y).c_str(), H(t.p.z).c_str());
        for (const V3& p : w.parking) h.p(p);
        for (const World::RegionStart& r : w.regionStarts) {
            h.u(r.map), h.p(r.spawn), h.mm(r.yaw);
            for (const V3& p : r.parking) h.p(p);
        }
        for (const World::SectionSpot& s : w.sections) h.s(s.name), h.p(s.entry), h.p(s.exit);
        out.count[PLACES] = w.stations.size() + w.spawnPads.size() + w.tags.size() + w.regionStarts.size() + w.sections.size();
    }
    {
        Fnv& h = f[TRAILS];
        for (const World::MarkedTrail& t : w.trails) {
            h.s(t.name), h.u(t.rating), h.u(t.pts.size());
            for (const V3& p : t.pts) h.p(p);
            D("trail %s %d %zu\n", t.name.c_str(), t.rating, t.pts.size());
            for (const V3& p : t.pts) D(" %s %s %s\n", H(p.x).c_str(), H(p.y).c_str(), H(p.z).c_str());
        }
        for (const std::vector<V3>& r : w.regionRoads) {
            h.u(r.size());
            for (const V3& p : r) h.p(p);
            D("road %zu\n", r.size());
            for (const V3& p : r) D(" %s %s %s\n", H(p.x).c_str(), H(p.y).c_str(), H(p.z).c_str());
        }
        for (const V3& p : w.loopRoad) h.p(p);
        out.count[TRAILS] = w.trails.size() + w.regionRoads.size();
    }
    out.count[ROCKS] = w.rocks.size(), out.count[TREES] = w.trees.size(), out.count[LOGS] = w.logs.size();
    out.count[BUSHES] = w.bushes.size(), out.count[BOXES] = w.boxes.size(), out.count[POLES] = w.poles.size();
    for (int c = 0; c < CAT_COUNT; c++) out.cat[c] = f[c].h;
    return out;
}

}  // namespace worldprint
