// The procedural world's map fog (docs/superpowers/specs/2026-10-25-procedural-world-design.md section 12): the land is endless,
// so the map starts dark and shows only what has been seen. Explored ground is kept in GLOBAL coordinates (so it survives the
// game's window sliding) as a sparse set of 1 km tiles, each a 64 x 64 bitset of 16 m cells, per seed; it is saved to a small
// file. The map's picture of revealed ground is made lazily from the generator, a tile at a time (no GL here).
#pragma once
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace proc {

class ProcWorld;

class Reveal {
public:
    static constexpr int kCell = 16;                  // m: one explored cell
    static constexpr int kTileCells = 64;             // cells a side in a tile
    static constexpr int kTile = kCell * kTileCells;  // m: a tile's side (1 km)

    // The radius seen round a vehicle or a walker: 250 m on the low ground, growing with its height over the land round about
    // (heightAbove, m) to 900 m from a high ridge.
    static double radiusFor(double heightAbove);
    // How high y (global) is over the average of the land on a ring round (gx, gz) (a coarse local average; never negative).
    static double heightAbove(const ProcWorld& g, double gx, double gz, double y);

    // Marks every cell whose middle is within r of (gx, gz); returns how many were new.
    int revealDisc(double gx, double gz, double r);
    bool cell(int64_t ci, int64_t cj) const;
    bool at(double gx, double gz) const;
    // A cell's visibility 0..1: its 3 x 3 neighbourhood blurred, so the edge of the reveal fades over a cell or two.
    float fade(int64_t ci, int64_t cj) const;
    bool hasTileNear(int64_t tx, int64_t tz) const;  // this tile or one of its 8 neighbours has anything
    size_t tiles() const { return tiles_.size(); }
    size_t cells() const;
    uint64_t revision() const { return rev_; }  // changes whenever a cell is added
    bool dirty = false;                         // changed since the last save/load
    void clear();

    // The file (versioned): "CZPM", version, the seed, then each tile. save() writes a temporary and renames it, making the folder
    // if needed; load() refuses a file of another seed or version (and leaves this empty).
    bool save(const std::string& path, uint64_t seed);
    bool load(const std::string& path, uint64_t seed);

    static int64_t cellOf(double g) { return (int64_t)std::floor(g / kCell); }

private:
    using Tile = std::array<uint64_t, kTileCells>;  // row b (z), bit a (x)
    using Key = std::pair<int64_t, int64_t>;
    std::map<Key, Tile> tiles_;
    uint64_t rev_ = 1;
    mutable Key lastKey_{INT64_MIN, INT64_MIN};
    mutable const Tile* last_ = nullptr;
    const Tile* find(int64_t tx, int64_t tz) const;
};

// The map's ground pictures: one 64 x 64 image a tile, a pixel a cell, made only where the cell is (nearly) revealed.
class MapImage {
public:
    struct Tile {
        std::array<uint8_t, Reveal::kTileCells * Reveal::kTileCells * 3> rgb{};
        std::array<uint8_t, Reveal::kTileCells * Reveal::kTileCells> alpha{};  // 0..255: how much of the ground shows (fade)
        std::bitset<Reveal::kTileCells * Reveal::kTileCells> made;
        uint64_t rev = 0;  // the Reveal revision alpha was computed at
        bool complete = false;
    };
    // Updates a tile's fade from the reveal and makes pixels (up to `ms` of work); returns the tile (null: nothing there to show).
    Tile* update(const ProcWorld& g, const Reveal& r, int64_t tx, int64_t tz, double ms);
    size_t tiles() const { return tiles_.size(); }
    void clear() { tiles_.clear(); }
    size_t madePixels() const;

    // The map's colour at a global point (the look of Coop::buildMap: biome ground, hillshade, rivers and creeks as water).
    static void colour(const ProcWorld& g, double gx, double gz, float out[3]);

private:
    std::map<std::pair<int64_t, int64_t>, Tile> tiles_;
};

}  // namespace proc
