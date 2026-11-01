#pragma once
// A sort that puts equal elements in the same order on every machine (docs/plans/xplat-world.md).
//
// std::sort leaves the order of elements that compare equal up to the library: libstdc++ (Linux, Windows' mingw-w64) and libc++
// (macOS) each do it their own way. Where world generation sorts with ties and the order picks something (which way round a scanned
// rock is fitted), a Mac's world wasn't a Linux one's. dm::sort is the same introsort as libstdc++'s std::sort (median of three to
// the front, an unguarded partition down to runs of 16, a heapsort past 2 log2 n levels, then one insertion sort), written out here,
// so it gives libstdc++'s order for ties everywhere: the worlds Linux and Windows built before stay as they were.
// Better still is a comparator with no ties at all (then any sort gives the one order); this is for where the order of ties
// already decided a world that is out there.
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace dm {

namespace sortdetail {
template <class It, class Cmp>
void moveMedianToFirst(It result, It a, It b, It c, Cmp& comp) {
    using std::iter_swap;
    if (comp(*a, *b)) {
        if (comp(*b, *c)) iter_swap(result, b);
        else if (comp(*a, *c)) iter_swap(result, c);
        else iter_swap(result, a);
    } else if (comp(*a, *c)) {
        iter_swap(result, a);
    } else if (comp(*b, *c)) {
        iter_swap(result, c);
    } else {
        iter_swap(result, b);
    }
}
template <class It, class Cmp>
It unguardedPartition(It first, It last, It pivot, Cmp& comp) {
    while (true) {
        while (comp(*first, *pivot)) ++first;
        --last;
        while (comp(*pivot, *last)) --last;
        if (!(first < last)) return first;
        std::iter_swap(first, last);
        ++first;
    }
}
template <class It, class Cmp>
void unguardedLinearInsert(It last, Cmp& comp) {
    auto val = std::move(*last);
    It next = last;
    --next;
    while (comp(val, *next)) {
        *last = std::move(*next);
        last = next;
        --next;
    }
    *last = std::move(val);
}
template <class It, class Cmp>
void insertionSort(It first, It last, Cmp& comp) {
    if (first == last) return;
    for (It i = first + 1; i != last; ++i) {
        if (comp(*i, *first)) {
            auto val = std::move(*i);
            std::move_backward(first, i, i + 1);
            *first = std::move(val);
        } else {
            unguardedLinearInsert(i, comp);
        }
    }
}
template <class It, class Cmp>
void finalInsertionSort(It first, It last, Cmp& comp) {
    if (last - first > 16) {
        insertionSort(first, first + 16, comp);
        for (It i = first + 16; i != last; ++i) unguardedLinearInsert(i, comp);
    } else {
        insertionSort(first, last, comp);
    }
}
// The heap (a binary max-heap, as libstdc++'s make_heap / pop_heap lay it out).
template <class It, class D, class T, class Cmp>
void pushHeap(It first, D hole, D top, T value, Cmp& comp) {
    D parent = (hole - 1) / 2;
    while (hole > top && comp(*(first + parent), value)) {
        *(first + hole) = std::move(*(first + parent));
        hole = parent;
        parent = (hole - 1) / 2;
    }
    *(first + hole) = std::move(value);
}
template <class It, class D, class T, class Cmp>
void adjustHeap(It first, D hole, D len, T value, Cmp& comp) {
    const D top = hole;
    D child = hole;
    while (child < (len - 1) / 2) {
        child = 2 * (child + 1);
        if (comp(*(first + child), *(first + (child - 1)))) child--;
        *(first + hole) = std::move(*(first + child));
        hole = child;
    }
    if ((len & 1) == 0 && child == (len - 2) / 2) {
        child = 2 * (child + 1);
        *(first + hole) = std::move(*(first + (child - 1)));
        hole = child - 1;
    }
    pushHeap(first, hole, top, std::move(value), comp);
}
template <class It, class Cmp>
void heapSort(It first, It last, Cmp& comp) {
    using D = typename std::iterator_traits<It>::difference_type;
    const D len = last - first;
    if (len >= 2) {  // make_heap
        for (D parent = (len - 2) / 2;; parent--) {
            auto value = std::move(*(first + parent));
            adjustHeap(first, parent, len, std::move(value), comp);
            if (parent == 0) break;
        }
    }
    while (last - first > 1) {  // sort_heap
        --last;
        auto value = std::move(*last);
        *last = std::move(*first);
        adjustHeap(first, D(0), D(last - first), std::move(value), comp);
    }
}
template <class It, class Cmp>
void introsortLoop(It first, It last, long depth, Cmp& comp) {
    while (last - first > 16) {
        if (depth == 0) {
            heapSort(first, last, comp);
            return;
        }
        --depth;
        It mid = first + (last - first) / 2;
        moveMedianToFirst(first, first + 1, mid, last - 1, comp);
        It cut = unguardedPartition(first + 1, last, first, comp);
        introsortLoop(cut, last, depth, comp);
        last = cut;
    }
}
inline long lg(long n) {
    long k = 0;
    while (n > 1) n >>= 1, k++;
    return k;
}
}  // namespace sortdetail

template <class It, class Cmp>
void sort(It first, It last, Cmp comp) {
    if (first == last) return;
    sortdetail::introsortLoop(first, last, sortdetail::lg((long)(last - first)) * 2, comp);
    sortdetail::finalInsertionSort(first, last, comp);
}

}  // namespace dm
