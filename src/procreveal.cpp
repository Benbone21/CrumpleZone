#include "procreveal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform.h"
#include "procworld.h"

namespace proc {

double Reveal::radiusFor(double h) {
    const double k = std::min(1.0, std::max(0.0, h) / 200.0);
    return 250 + 650 * k;
}

double Reveal::heightAbove(const ProcWorld& g, double gx, double gz, double y) {
    double sum = 0;
    const int n = 8;
    for (int i = 0; i < n; i++) {
        const double a = i * (2 * M_PI / n);
        sum += g.macroCached(gx + 400 * std::cos(a), gz + 400 * std::sin(a)).h;  // (never waits on an erosion tile: the game thread)
    }
    return std::max(0.0, y - sum / n);
}

const Reveal::Tile* Reveal::find(int64_t tx, int64_t tz) const {
    const Key k{tx, tz};
    if (last_ && k == lastKey_) return last_;
    auto it = tiles_.find(k);
    if (it == tiles_.end()) return nullptr;  // (not cached: a miss is cheap and the pointer would dangle never, but keep it simple)
    lastKey_ = k, last_ = &it->second;
    return last_;
}

int Reveal::revealDisc(double gx, double gz, double r) {
    int added = 0;
    const int64_t c0 = cellOf(gx - r), c1 = cellOf(gx + r), d0 = cellOf(gz - r), d1 = cellOf(gz + r);
    for (int64_t cj = d0; cj <= d1; cj++) {
        const double cz = (cj + 0.5) * kCell - gz;
        const double rem = r * r - cz * cz;
        if (rem < 0) continue;
        for (int64_t ci = c0; ci <= c1; ci++) {
            const double cx = (ci + 0.5) * kCell - gx;
            if (cx * cx > rem) continue;
            const int64_t tx = floorDiv(ci, kTileCells), tz = floorDiv(cj, kTileCells);
            auto it = tiles_.find({tx, tz});
            if (it == tiles_.end()) {
                it = tiles_.emplace(Key{tx, tz}, Tile{}).first;
                last_ = nullptr, lastKey_ = {INT64_MIN, INT64_MIN};
            }
            const int a = (int)(ci - tx * kTileCells), b = (int)(cj - tz * kTileCells);
            uint64_t& row = it->second[b];
            if (!(row >> a & 1)) row |= 1ull << a, added++;
        }
    }
    if (added) rev_++, dirty = true;
    return added;
}

bool Reveal::cell(int64_t ci, int64_t cj) const {
    const int64_t tx = floorDiv(ci, kTileCells), tz = floorDiv(cj, kTileCells);
    const Tile* t = find(tx, tz);
    if (!t) return false;
    return (*t)[(int)(cj - tz * kTileCells)] >> (int)(ci - tx * kTileCells) & 1;
}

bool Reveal::at(double gx, double gz) const { return cell(cellOf(gx), cellOf(gz)); }

float Reveal::fade(int64_t ci, int64_t cj) const {
    static const int w[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    int s = 0;
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++)
            if (cell(ci + di, cj + dj)) s += w[dj + 1][di + 1];
    return s / 16.0f;
}

bool Reveal::hasTileNear(int64_t tx, int64_t tz) const {
    for (int64_t z = tz - 1; z <= tz + 1; z++)
        for (int64_t x = tx - 1; x <= tx + 1; x++)
            if (tiles_.count({x, z})) return true;
    return false;
}

size_t Reveal::cells() const {
    size_t n = 0;
    for (const auto& kv : tiles_)
        for (uint64_t row : kv.second) n += (size_t)__builtin_popcountll(row);
    return n;
}

void Reveal::clear() {
    tiles_.clear();
    last_ = nullptr, lastKey_ = {INT64_MIN, INT64_MIN};
    rev_++, dirty = false;
}

// File: "CZPM" u8 version(1) u8 pad[3] u64 seed u32 tileCount, then per tile: i64 tx, i64 tz, 64 x u64 rows (all little-endian).
static void put(std::string& s, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; i++) s.push_back((char)(v >> (8 * i) & 0xff));
}
static bool get(const std::string& s, size_t& at, uint64_t& v, int bytes) {
    if (at + bytes > s.size()) return false;
    v = 0;
    for (int i = 0; i < bytes; i++) v |= (uint64_t)(uint8_t)s[at + i] << (8 * i);
    at += bytes;
    return true;
}

bool Reveal::save(const std::string& path, uint64_t seed) {
    std::string out = "CZPM";
    put(out, 1, 1), put(out, 0, 3), put(out, seed, 8), put(out, tiles_.size(), 4);
    for (const auto& kv : tiles_) {
        put(out, (uint64_t)kv.first.first, 8), put(out, (uint64_t)kv.first.second, 8);
        for (uint64_t row : kv.second) put(out, row, 8);
    }
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && slash > 0) makeDir(path.substr(0, slash));
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) return false;
    std::remove(path.c_str());  // (Windows' rename won't replace)
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    dirty = false;
    return true;
}

bool Reveal::load(const std::string& path, uint64_t seed) {
    clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string in;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) in.append(buf, n);
    std::fclose(f);
    size_t at = 0;
    uint64_t ver, pad, sd, count;
    if (in.size() < 4 || in.compare(0, 4, "CZPM") != 0) return false;
    at = 4;
    if (!get(in, at, ver, 1) || ver != 1 || !get(in, at, pad, 3) || !get(in, at, sd, 8) || sd != seed || !get(in, at, count, 4)) return false;
    if (count > 1000000) return false;
    std::map<Key, Tile> loaded;
    for (uint64_t k = 0; k < count; k++) {
        uint64_t tx, tz;
        Tile t;
        if (!get(in, at, tx, 8) || !get(in, at, tz, 8)) return false;
        for (uint64_t& row : t)
            if (!get(in, at, row, 8)) return false;
        loaded[{(int64_t)tx, (int64_t)tz}] = t;
    }
    tiles_ = std::move(loaded);
    last_ = nullptr, lastKey_ = {INT64_MIN, INT64_MIN};
    rev_++, dirty = false;
    return true;
}

// ---- The map's pictures ----------------------------------------------------------------------

void MapImage::colour(const ProcWorld& g, double gx, double gz, float out[3]) {
    static const float kBase[PB_COUNT][3] = {{0.33f, 0.47f, 0.24f}, {0.20f, 0.34f, 0.19f}, {0.72f, 0.44f, 0.29f}, {0.55f, 0.52f, 0.48f}, {0.86f, 0.74f, 0.52f}};
    const Macro m = g.macroCached(gx, gz);  // (the map: worn where its erosion tiles are made, never waiting on one)
    float c[3] = {0, 0, 0};
    for (int b = 0; b < PB_COUNT; b++)
        for (int k = 0; k < 3; k++) c[k] += (float)m.w[b] * kBase[b][k];
    const double h = g.groundHeight(gx, gz, m);
    // Snow on the high mountains.
    if (m.w[PB_MOUNTAINS] > 0.4) {
        const float s = (float)std::min(1.0, std::max(0.0, (m.h - 380) / 120.0) * m.w[PB_MOUNTAINS]);
        for (int k = 0; k < 3; k++) c[k] = c[k] * (1 - s) + (k == 2 ? 0.98f : 0.94f) * s;
    }
    // Hillshade (light from the north-west), from the smooth land.
    const double e = Reveal::kCell;
    const double dx = g.macroCached(gx + e, gz).h - g.macroCached(gx - e, gz).h, dz = g.macroCached(gx, gz + e).h - g.macroCached(gx, gz - e).h;
    const double shade = std::min(1.35, std::max(0.45, 1.0 + (-dx * 0.7 + dz * 0.7) / (2 * e) * 1.4));
    for (float& v : c) v = (float)std::min(1.0, std::max(0.0, v * shade * (0.92 + 0.0006 * h)));
    // Rivers and creeks.
    const double rw = m.riverOn > 0.5 ? m.riverHW : 0, cw = m.creekOn > 0.5 ? m.creekHW : 0;
    const double half = (double)Reveal::kCell * 0.5;
    const bool river = rw > 0 && std::fabs(m.riverS) < std::max(rw, half), creek = cw > 0 && std::fabs(m.creekS) < std::max(cw, half * 0.6);
    if (river || creek) {
        const float k = river ? 1.0f : 0.8f;
        c[0] = c[0] * (1 - k) + 0.15f * k, c[1] = c[1] * (1 - k) + 0.35f * k, c[2] = c[2] * (1 - k) + 0.55f * k;
    }
    for (int k = 0; k < 3; k++) out[k] = c[k];
}

MapImage::Tile* MapImage::update(const ProcWorld& g, const Reveal& r, int64_t tx, int64_t tz, double ms) {
    constexpr int N = Reveal::kTileCells;
    if (!r.hasTileNear(tx, tz)) return nullptr;
    Tile& t = tiles_[{tx, tz}];
    if (t.rev != r.revision()) {  // (the fade of every pixel, again: something new was seen)
        t.rev = r.revision();
        t.complete = false;
        for (int b = 0; b < N; b++)
            for (int a = 0; a < N; a++) t.alpha[b * N + a] = (uint8_t)std::lround(r.fade(tx * N + a, tz * N + b) * 255);
    }
    if (t.complete) return &t;
    const auto t0 = std::chrono::steady_clock::now();
    int n = 0;
    bool all = true;
    for (int b = 0; b < N && all; b++)
        for (int a = 0; a < N; a++) {
            const int i = b * N + a;
            if (t.alpha[i] == 0 || t.made[i]) continue;
            if ((++n & 15) == 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > ms) {
                all = false;
                break;
            }
            float c[3];
            colour(g, (tx * N + a + 0.5) * Reveal::kCell, (tz * N + b + 0.5) * Reveal::kCell, c);
            for (int k = 0; k < 3; k++) t.rgb[i * 3 + k] = (uint8_t)std::lround(c[k] * 255);
            t.made[i] = true;
        }
    t.complete = all;
    return &t;
}

size_t MapImage::madePixels() const {
    size_t n = 0;
    for (const auto& kv : tiles_) n += kv.second.made.count();
    return n;
}

}  // namespace proc
