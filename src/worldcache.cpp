#include "worldcache.h"

#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>

#include "bytepack.h"
#include "meshcache.h"
#include "props.h"
#include "world.h"

namespace {
const char kMagic[8] = {'C', 'Z', 'W', 'O', 'R', 'L', 'D', '1'};
constexpr uint64_t kFormat = 4;  // bump with any change to what's written below or to how a map is built that the build id misses

struct Out {
    std::vector<uint8_t> b;
    std::vector<std::pair<uint64_t, uint64_t>> shuffled;  // 32-bit runs (at, count) to transform before packing (the heights)
    template <class T> void pod(const T& v) {
        static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value, "field by field");
        const size_t at = b.size();
        b.resize(at + sizeof v);
        std::memcpy(b.data() + at, &v, sizeof v);
    }
    void v3(const V3& p) { pod(p.x), pod(p.y), pod(p.z); }
    void str(const std::string& s) {
        pod((uint32_t)s.size());
        b.insert(b.end(), s.begin(), s.end());
    }
    template <class T> void arr(const std::vector<T>& v) {  // (plain numbers, in one go)
        static_assert(std::is_arithmetic<T>::value, "numbers");
        pod((uint64_t)v.size());
        const size_t at = b.size();
        b.resize(at + v.size() * sizeof(T));
        if (!v.empty()) std::memcpy(b.data() + at, v.data(), v.size() * sizeof(T));
    }
    template <class T, class F> void list(const std::vector<T>& v, F each) {
        pod((uint64_t)v.size());
        for (const T& x : v) each(x);
    }
};

struct In {
    const std::vector<uint8_t>& b;
    size_t at = 0;
    bool ok = true;
    explicit In(const std::vector<uint8_t>& bytes) : b(bytes) {}
    template <class T> void pod(T& v) {
        if (!ok || at + sizeof v > b.size()) {
            ok = false;
            return;
        }
        std::memcpy(&v, b.data() + at, sizeof v);
        at += sizeof v;
    }
    void v3(V3& p) { pod(p.x), pod(p.y), pod(p.z); }
    void str(std::string& s) {
        uint32_t n = 0;
        pod(n);
        if (!ok || at + n > b.size()) {
            ok = false;
            return;
        }
        s.assign((const char*)b.data() + at, n);
        at += n;
    }
    bool count(uint64_t& n, size_t each) {  // (a sane count: not more than the bytes left could hold)
        pod(n);
        if (ok && n * std::max<size_t>(each, 1) > b.size() - at) ok = false;
        return ok;
    }
    template <class T> void arr(std::vector<T>& v) {
        uint64_t n = 0;
        if (!count(n, sizeof(T))) return;
        v.resize(n);
        if (n) std::memcpy(v.data(), b.data() + at, n * sizeof(T));
        at += n * sizeof(T);
    }
    template <class T, class F> void list(std::vector<T>& v, F each) {
        uint64_t n = 0;
        if (!count(n, 1)) return;
        v.assign(n, T());
        for (T& x : v) {
            if (!ok) return;
            each(x);
        }
    }
};

// Station names are string constants in the code; a loaded one's lives here (for good: a handful of names per map).
const char* intern(const std::string& s) {
    static std::mutex lock;
    static std::set<std::string> names;
    std::lock_guard<std::mutex> lk(lock);
    return names.insert(s).first->c_str();
}
}  // namespace

// Everything a build leaves in a World, field by field (the lookup grids are rebuilt by buildGrid after a load). The same
// code writes and reads: `io` is an Out or an In, `w` const or not.
struct WorldIO {
    template <class IO, class W> static void fields(IO& io, W& w) {
        io.pod(w.map), io.pod(w.half), io.pod(w.hN);
        heights(io, w), io.arr(w.surf), io.arr(w.paint);
        tiles(io, w.roadLat), tiles(io, w.roadAlong), tiles(io, w.roadNear);
        tiles(io, w.waterY), tiles(io, w.flowX), tiles(io, w.flowZ);
        io.list(w.boxes, [&](auto& x) {
            io.v3(x.c), io.v3(x.h);
            for (auto& a : x.ax) io.v3(a);
            for (auto& c : x.col) io.pod(c);
            io.pod(x.bound), io.pod(x.hidden), io.pod(x.solid), io.pod(x.rock);
        });
        io.list(w.loopRoad, [&](auto& p) { io.v3(p); });
        io.arr(w.loopAlong), io.pod(w.loopLength);
        io.list(w.trees, [&](auto& t) { io.v3(t.base), io.pod(t.height), io.pod(t.radius), io.pod(t.tint), io.pod(t.kind), io.pod(t.design); });
        io.list(w.poles, [&](auto& p) { io.pod(p.x), io.pod(p.z), io.pod(p.y0), io.pod(p.y1), io.pod(p.r); });
        io.list(w.rocks, [&](auto& r) {
            io.v3(r.c), io.pod(r.r), io.pod(r.tint), io.pod(r.seed);
            io.list(r.v, [&](auto& p) { io.v3(p); });
            io.list(r.f, [&](auto& f) { io.pod(f[0]), io.pod(f[1]), io.pod(f[2]); });
            faces(io, r);
            io.pod(r.scan);
            for (auto& a : r.axis) io.v3(a);
            io.v3(r.origin), io.v3(r.at);
        });
        io.list(w.logs, [&](auto& l) { io.v3(l.a), io.v3(l.b), io.pod(l.r); });
        io.list(w.bushes, [&](auto& b) { io.v3(b.p), io.pod(b.r), io.pod(b.tint), io.pod(b.kind); });
        io.list(w.waters, [&](auto& l) { io.pod(l.x), io.pod(l.z), io.pod(l.r), io.pod(l.y); });
        io.list(w.fixtures, [&](auto& f) {
            io.pod(f.kind), io.v3(f.a), io.v3(f.b), io.pod(f.yaw);
            for (auto& c : f.col) io.pod(c);
        });
        io.list(w.models, [&](auto& m) {
            io.str(m.id), io.v3(m.p), io.pod(m.yaw), io.v3(m.scale);
            io.list(m.set, [&](auto& kv) { io.str(kv.first), io.pod(kv.second); });
            io.pod(m.variant), io.pod(m.oriented);
            for (auto& a : m.ax) io.v3(a);
            io.pod(m.bare), io.pod(m.sunlit), io.pod(m.wear), io.pod(m.clip), io.pod(m.clipLo), io.pod(m.clipHi);
        });
        io.list(w.tags, [&](auto& t) { io.str(t.text), io.v3(t.p); });
        io.pod(w.parkEdited), io.pod(w.editStamp);
        io.list(w.sections, [&](auto& s) { io.str(s.name), io.pod(s.type), io.v3(s.entry), io.v3(s.exit), io.pod(s.heading), io.v3(s.spawn), io.pod(s.spawnYaw); });
        io.list(w.stations, [&](auto& s) { io.v3(s.p), io.pod(s.yaw), station(io, s), io.pod(s.r); });
        io.v3(w.spawn), io.pod(w.spawnYaw);
        io.list(w.spawnPads, [&](auto& p) {
            io.v3(p.p), io.pod(p.yaw), io.pod(p.back), io.pod(p.front), io.pod(p.half), io.pod(p.ramp), io.pod(p.drop);
            io.pod(p.slots), io.pod(p.cols), io.pod(p.openAhead), io.pod(p.drawn);
        });
        io.list(w.parking, [&](auto& p) { io.v3(p); });
        io.list(w.rivers, [&](auto& r) {
            io.str(r.name);
            io.list(r.spots, [&](auto& p) { io.v3(p.p), io.pod(p.depth), io.pod(p.halfWidth), io.pod(p.speed), io.pod(p.cls), io.pod(p.bed); });
        });
        io.list(w.falls, [&](auto& f) {
            io.str(f.name), io.v3(f.lip), io.pod(f.yaw), io.pod(f.height), io.pod(f.width), io.pod(f.throwOut), io.pod(f.variant), io.pod(f.seed);
            io.pod(f.poolR), io.pod(f.poolDepth);
        });
        io.pod(w.parkingYaw), io.pod(w.crashProps), io.pod(w.wrapPeriod), io.pod(w.cellEdits);
    }
    static bool sane(const World& w) {
        const size_t n = (size_t)w.hN * w.hN;
        auto fits = [&](size_t k) { return k == 0 || k == n; };
        auto tilesFit = [&](int side) { return side == 0 || side == w.hN; };
        return w.hN >= 2 && w.hgrid.size() == n && fits(w.surf.size()) && fits(w.paint.size()) && tilesFit(w.roadLat.side()) &&
               tilesFit(w.roadAlong.side()) && tilesFit(w.roadNear.side()) && tilesFit(w.waterY.side()) && tilesFit(w.flowX.side()) &&
               tilesFit(w.flowZ.side());
    }
    // The heights: noted for the delta-and-shuffle that makes them pack small.
    // (Where the map's land before anything was laid on it can be made again, each height is stored as its bits' difference
    // (xor) from that: zero over most of the map, which packs to next to nothing.)
    static bool baseLand(const World& w, std::vector<float>& base) {
        if (w.map != MAP_BACKCOUNTRY || w.hN < 2) return false;
        backcountryLand(w.half, w.hN, base);
        return base.size() == w.hgrid.size();
    }
    static void xorWith(std::vector<float>& h, const std::vector<float>& base) {
        for (size_t k = 0; k < h.size(); k++) {
            uint32_t a, b;
            std::memcpy(&a, &h[k], 4), std::memcpy(&b, &base[k], 4);
            a ^= b;
            std::memcpy(&h[k], &a, 4);
        }
    }
    static void heights(Out& o, const World& w) {
        std::vector<float> base, h = w.hgrid;
        const bool predicted = baseLand(w, base);
        if (predicted) xorWith(h, base);
        o.pod(predicted);
        o.arr(h);
        o.shuffled.push_back({o.b.size() - h.size() * 4, h.size()});
    }
    static void heights(In& in, World& w) {
        bool predicted = false;
        in.pod(predicted);
        in.arr(w.hgrid);
        std::vector<float> base;
        if (in.ok && predicted && !(baseLand(w, base) && (xorWith(w.hgrid, base), true))) in.ok = false;
    }
    // A rock's face planes: left out when they're what its corners and faces give (most rocks), and worked out again.
    static void derive(const Rock& r, std::vector<V3>& fn, std::vector<double>& fd) {
        fn.clear(), fd.clear();
        for (const auto& f : r.f) {
            if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= (int)r.v.size() || f[1] >= (int)r.v.size() || f[2] >= (int)r.v.size()) return;
            const V3 nrm = norm(cross(r.v[f[1]] - r.v[f[0]], r.v[f[2]] - r.v[f[0]]));
            fn.push_back(nrm), fd.push_back(dot(nrm, r.v[f[0]]));
        }
    }
    static void faces(Out& o, const Rock& r) {
        std::vector<V3> fn;
        std::vector<double> fd;
        derive(r, fn, fd);
        bool same = fn.size() == r.fn.size() && fd.size() == r.fd.size();
        for (size_t k = 0; same && k < fn.size(); k++)
            same = std::memcmp(&fn[k], &r.fn[k], sizeof(V3)) == 0 && std::memcmp(&fd[k], &r.fd[k], sizeof(double)) == 0;
        o.pod(same);
        if (same) return;
        o.list(r.fn, [&](const V3& p) { o.v3(p); });
        o.arr(r.fd);
    }
    static void faces(In& in, Rock& r) {
        bool same = false;
        in.pod(same);
        if (same) return derive(r, r.fn, r.fd);
        in.list(r.fn, [&](V3& p) { in.v3(p); });
        in.arr(r.fd);
    }
    static void station(Out& o, const Station& s) { o.str(s.name ? s.name : ""); }
    static void station(In& in, Station& s) {
        std::string n;
        in.str(n);
        s.name = intern(n);
    }
    template <class T> static void tiles(Out& o, const CellTiles<T>& t) {
        o.pod((int32_t)t.side()), o.pod(t.fallback());
        for (size_t k = 0; k < t.tilesAll(); k++) o.arr(t.tile(k));
    }
    template <class T> static void tiles(In& in, CellTiles<T>& t) {
        int32_t side = 0;
        T def = T();
        in.pod(side), in.pod(def);
        if (!in.ok || side < 0 || side > 100000) {
            in.ok = false;
            return;
        }
        t.reset(side, def);
        for (size_t k = 0; k < t.tilesAll() && in.ok; k++) {
            std::vector<T> cells;
            in.arr(cells);
            if (in.ok && !t.setTile(k, std::move(cells))) in.ok = false;
        }
    }
};

namespace worldcache {

namespace {
std::string fileFor(int map) { return "world-" + std::to_string(map); }
}  // namespace

bool cacheable(int map) {
    return (map == MAP_FRONTIER || map == MAP_BACKCOUNTRY || map == MAP_MOUNTAIN) && !bypass && World::halfSizeOverride <= 0 &&
           meshcache::enabled();
}

uint64_t keyFor(int map) {
    if (!cacheable(map)) return 0;
    meshcache::Hasher h;
    h.bytes(kMagic, sizeof kMagic), h.u64(kFormat), h.u64(meshcache::codeVersion());
    h.u64((uint64_t)map), h.num(World::halfSizeFor(map));
    // The scanned rocks in use (the rocks' hulls come from them): which, and what they are.
    if (const props::HullLibrary* lib = props::activeRockScans()) {
        h.u64(lib->hulls.size());
        for (const props::RockHull& r : lib->hulls) {
            h.str(r.name);
            for (const V3& p : r.v) h.num(p.x), h.num(p.y), h.num(p.z);
        }
    } else {
        h.u64(0);
    }
    return h.h;
}

// The file: the magic, the format, the transformed runs, then the packed fields (bytepack.h).
std::vector<uint8_t> encode(const World& w) {
    Out o;
    WorldIO::fields(o, w);
    for (const auto& r : o.shuffled) bytepack::deltaShuffle32(o.b, r.first, r.second);
    Out head;
    head.b.insert(head.b.end(), kMagic, kMagic + sizeof kMagic);
    head.pod(kFormat);
    head.pod((uint64_t)o.shuffled.size());
    for (const auto& r : o.shuffled) head.pod(r.first), head.pod(r.second);
    const std::vector<uint8_t> packed = bytepack::pack(o.b);
    head.b.insert(head.b.end(), packed.begin(), packed.end());
    return std::move(head.b);
}

bool decode(const std::vector<uint8_t>& bytes, World& w) {
    if (bytes.size() < sizeof kMagic + 16 || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) return false;
    In head(bytes);
    head.at = sizeof kMagic;
    uint64_t format = 0, runs = 0;
    head.pod(format), head.pod(runs);
    if (!head.ok || format != kFormat || runs > 64) return false;
    std::vector<std::pair<uint64_t, uint64_t>> shuffled(runs);
    for (auto& r : shuffled) head.pod(r.first), head.pod(r.second);
    if (!head.ok) return false;
    std::vector<uint8_t> body;
    if (!bytepack::unpack(std::vector<uint8_t>(bytes.begin() + (long)head.at, bytes.end()), body)) return false;
    for (const auto& r : shuffled) {
        if (r.second > body.size() / 4 || r.first > body.size() - r.second * 4) return false;
        bytepack::unshuffleDelta32(body, r.first, r.second);
    }
    In in(body);
    World tmp(w);  // (filled in a copy: a damaged file leaves `w` as it was)
    WorldIO::fields(in, tmp);
    if (!in.ok || in.at != body.size()) return false;
    if (!WorldIO::sane(tmp)) return false;
    w = std::move(tmp);
    return true;
}

bool load(World& w) {
    const uint64_t key = keyFor(w.map);
    if (!key) return false;
    std::vector<uint8_t> bytes;
    if (!meshcache::getNamedBlob(fileFor(w.map), key, bytes)) return false;
    const int map = w.map;
    const double half = w.halfSize();
    if (!decode(bytes, w)) return false;
    return w.map == map && w.halfSize() == half;
}

void store(const World& w) {
    const uint64_t key = keyFor(w.map);
    if (key) meshcache::putNamedBlob(fileFor(w.map), key, encode(w));
}

}  // namespace worldcache
