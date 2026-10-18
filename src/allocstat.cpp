#include "allocstat.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <new>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif
#if defined(__GLIBC__)
#include <execinfo.h>
#define CZ_BACKTRACE 1
#endif

namespace {
thread_local uint64_t tCount = 0;
std::atomic<uint64_t> gCount{0};
std::atomic<bool> gTrace{false};
thread_local bool inTrace = false;
std::mutex gMu;
std::map<std::vector<void*>, uint64_t>* gStacks = nullptr;  // (leaked on purpose: allocations can outlive static destruction)

inline void note() {
    tCount++;
    gCount.fetch_add(1, std::memory_order_relaxed);
#ifdef CZ_BACKTRACE
    if (gTrace.load(std::memory_order_relaxed) && !inTrace) {
        inTrace = true;
        void* pcs[16];
        const int n = backtrace(pcs, 16);
        {
            std::lock_guard<std::mutex> lock(gMu);
            if (!gStacks) gStacks = new std::map<std::vector<void*>, uint64_t>();
            (*gStacks)[std::vector<void*>(pcs + 2, pcs + n)]++;  // (skipping note and operator new)
        }
        inTrace = false;
    }
#endif
}

void* grab(std::size_t n, std::size_t align) {
    note();
    if (n == 0) n = 1;
    void* p = nullptr;
    if (align <= alignof(std::max_align_t)) p = std::malloc(n);
    else {
#ifdef _WIN32
        p = _aligned_malloc(n, align);
#else
        if (posix_memalign(&p, align, n) != 0) p = nullptr;
#endif
    }
    if (!p) throw std::bad_alloc();
    return p;
}
void release(void* p, std::size_t align) {
#ifdef _WIN32
    if (align > alignof(std::max_align_t)) return _aligned_free(p);
#else
    (void)align;
#endif
    std::free(p);
}
}  // namespace

namespace allocstat {
uint64_t count() { return tCount; }
uint64_t countAll() { return gCount.load(std::memory_order_relaxed); }
void trace(bool on) { gTrace = on; }
void dump(FILE* f, int top) {
#ifdef CZ_BACKTRACE
    gTrace = false;
    std::vector<std::pair<uint64_t, const std::vector<void*>*>> v;
    {
        std::lock_guard<std::mutex> lock(gMu);
        if (!gStacks) return;
        for (auto& kv : *gStacks) v.push_back({kv.second, &kv.first});
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (int i = 0; i < top && i < (int)v.size(); i++) {
        std::fprintf(f, "ALLOCSITE %llu\n", (unsigned long long)v[i].first);
        char** names = backtrace_symbols(v[i].second->data(), (int)v[i].second->size());
        for (size_t k = 0; names && k < v[i].second->size(); k++) std::fprintf(f, "  %s\n", names[k]);
        std::free(names);
    }
#else
    (void)f, (void)top;
#endif
}
}  // namespace allocstat

void* operator new(std::size_t n) { return grab(n, 0); }
void* operator new[](std::size_t n) { return grab(n, 0); }
void* operator new(std::size_t n, std::align_val_t a) { return grab(n, (std::size_t)a); }
void* operator new[](std::size_t n, std::align_val_t a) { return grab(n, (std::size_t)a); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return grab(n, 0); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return grab(n, 0); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { release(p, 0); }
void operator delete[](void* p) noexcept { release(p, 0); }
void operator delete(void* p, std::size_t) noexcept { release(p, 0); }
void operator delete[](void* p, std::size_t) noexcept { release(p, 0); }
void operator delete(void* p, std::align_val_t a) noexcept { release(p, (std::size_t)a); }
void operator delete[](void* p, std::align_val_t a) noexcept { release(p, (std::size_t)a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { release(p, (std::size_t)a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { release(p, (std::size_t)a); }
