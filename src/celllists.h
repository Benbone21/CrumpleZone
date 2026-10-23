#pragma once
// Lists of items per cell of a grid, packed (CSR: one array of items, an offset per cell): a world's lookup grids. Built in
// one go from a function that emits (cell, item) pairs, in the order a cell's list is to be read (it's called twice: to
// count, then to fill). A cell's list reads like a vector's: for (int i : lists[cell]).
#include <cstdint>
#include <vector>

class CellLists {
public:
    struct Range {
        const int *b, *e;
        const int* begin() const { return b; }
        const int* end() const { return e; }
        bool empty() const { return b == e; }
        size_t size() const { return (size_t)(e - b); }
    };
    template <class Emit>
    void build(int cells, Emit each) {
        start_.assign((size_t)cells + 1, 0);
        each([&](int cell, int) { start_[(size_t)cell + 1]++; });
        for (size_t c = 0; c < (size_t)cells; c++) start_[c + 1] += start_[c];
        items_.assign(start_.back(), 0);
        std::vector<uint32_t> at(start_.begin(), start_.end() - 1);
        each([&](int cell, int item) { items_[at[(size_t)cell]++] = item; });
    }
    Range operator[](size_t cell) const { return {items_.data() + start_[cell], items_.data() + start_[cell + 1]}; }
    bool empty() const { return start_.empty(); }
    void clear() { start_.clear(), items_.clear(); }

private:
    std::vector<uint32_t> start_;
    std::vector<int> items_;
};
