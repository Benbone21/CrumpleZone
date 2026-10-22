#pragma once
// Runs f(i) for every i in [0, n) on a few threads: for loops over the terrain grid's rows where each row only writes its own
// cells (a map's build), so the result is the same on any number of threads. Small jobs run on the calling thread.
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// Tests: at most this many threads on this thread's calls (0: as many as the machine has, up to 16).
inline thread_local int parallelRowsCap = 0;

template <class F>
void parallelRows(int n, F&& f, int minRowsPerThread = 64) {
    int hw = (int)std::max(1u, std::thread::hardware_concurrency());
    if (parallelRowsCap > 0) hw = std::min(hw, parallelRowsCap);
    const int threads = std::min({hw, 16, std::max(1, n / std::max(1, minRowsPerThread))});
    if (threads <= 1) {
        for (int i = 0; i < n; i++) f(i);
        return;
    }
    std::atomic<int> next{0};
    auto work = [&] {
        for (int i0; (i0 = next.fetch_add(8)) < n;)
            for (int i = i0; i < std::min(n, i0 + 8); i++) f(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (int t = 1; t < threads; t++) pool.emplace_back(work);
    work();
    for (std::thread& t : pool) t.join();
}
