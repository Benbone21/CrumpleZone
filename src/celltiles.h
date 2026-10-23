#pragma once
// A per-cell layer of the terrain grid (World's roads and water) stored only where it says something: the grid is cut into
// 64 x 64 cell tiles and a tile is kept only once a cell in it is set to something other than the layer's default (no road,
// dry). Indexed like the dense vector it replaces, by World::cellIndex (i * side + j). On a big map most of the land has no
// road or water on it, so this keeps those layers to a small share of the grid (docs/plans/big-maps.md, 3.2).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

template <class T>
class CellTiles {
public:
    static constexpr int kShift = 6, kTile = 1 << kShift, kMask = kTile - 1;

    // A cell to write: setting it to the default where its tile isn't kept keeps nothing.
    class Ref {
    public:
        Ref(CellTiles& g, int id) : g_(g), id_(id) {}
        operator T() const { return static_cast<const CellTiles&>(g_)[id_]; }
        T operator=(T v) {
            g_.set(id_, v);
            return v;
        }
        T operator=(const Ref& o) { return *this = (T)o; }

    private:
        CellTiles& g_;
        int id_;
    };

    // `side` cells a side, every one `def`. (side 0: off, empty() like an empty vector.)
    void reset(int side, T def) {
        side_ = side, def_ = def;
        per_ = (side + kMask) >> kShift;
        tiles_.assign((size_t)per_ * per_, std::vector<T>());
    }
    void clear() { reset(0, T()); }
    bool empty() const { return side_ == 0; }
    size_t size() const { return (size_t)side_ * side_; }
    int side() const { return side_; }
    T fallback() const { return def_; }

    T operator[](int id) const {
        const int i = id / side_, j = id - i * side_;
        const std::vector<T>& t = tiles_[(size_t)(i >> kShift) * per_ + (j >> kShift)];
        return t.empty() ? def_ : t[(size_t)((i & kMask) << kShift | (j & kMask))];
    }
    Ref operator[](int id) { return Ref(*this, id); }

    void set(int id, T v) {
        const int i = id / side_, j = id - i * side_;
        std::vector<T>& t = tiles_[(size_t)(i >> kShift) * per_ + (j >> kShift)];
        if (t.empty()) {
            if (v == def_) return;
            t.assign((size_t)kTile * kTile, def_);
        }
        t[(size_t)((i & kMask) << kShift | (j & kMask))] = v;
    }

    // The tiles themselves (row-major, tilesAll() of them; an empty one is all the default): for saving and loading a world.
    const std::vector<T>& tile(size_t k) const { return tiles_[k]; }
    bool setTile(size_t k, std::vector<T> cells) {
        if (k >= tiles_.size() || (!cells.empty() && cells.size() != (size_t)kTile * kTile)) return false;
        tiles_[k] = std::move(cells);
        return true;
    }

    // How many tiles are kept, of how many; and the bytes they take.
    size_t tilesKept() const {
        size_t k = 0;
        for (const std::vector<T>& t : tiles_) k += !t.empty();
        return k;
    }
    size_t tilesAll() const { return tiles_.size(); }
    size_t bytes() const { return tilesKept() * kTile * kTile * sizeof(T) + tiles_.size() * sizeof(std::vector<T>); }

private:
    int side_ = 0, per_ = 0;
    T def_ = T();
    std::vector<std::vector<T>> tiles_;
};
