#include "physics_workers.h"
#include <algorithm>
#include <chrono>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace {
// How long a thread waits by spinning before it blocks: a few substeps' worth.
constexpr std::chrono::microseconds kSpin(150);
inline void relax() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#else
    std::this_thread::yield();
#endif
}
// Spins until done() holds or the spin budget runs out; true if it held.
template <class Pred> bool spinUntil(Pred done) {
    const auto stop = std::chrono::steady_clock::now() + kSpin;
    for (;;) {
        for (int k = 0; k < 64; k++) {
            if (done()) return true;
            relax();
        }
        if (std::chrono::steady_clock::now() >= stop) return done();
    }
}
}  // namespace

int physicsThreadLimit() {
    return std::max(1, (int)std::thread::hardware_concurrency());
}

int resolvePhysicsThreads(int requested, int available) {
    available = std::max(1, available);
    if (requested <= 0) return std::min(4, std::max(1, available / 2));
    return std::min(requested, available);
}

PhysicsWorkers::PhysicsWorkers(int threads) {
    try {
        for (int i = 1; i < threads; i++) workers.emplace_back([this, i] {
            unsigned seen = 0;  // (not generation.load(): a worker that starts late must still take the batch already published)
            for (;;) {
                if (!spinUntil([&] { return generation.load() != seen || stopping.load(); })) {
                    std::unique_lock<std::mutex> lock(mutex);
                    sleepers++;
                    start.wait(lock, [&] { return generation.load() != seen || stopping.load(); });
                    sleepers--;
                }
                if (stopping) return;
                seen = generation.load();
                process(i);
                if (pending.fetch_sub(1) == 1 && callerSleeping.load()) {
                    std::lock_guard<std::mutex> lock(mutex);
                    done.notify_one();
                }
            }
        });
    } catch (...) {
        stop();
        throw;
    }
}

void PhysicsWorkers::process(int self) {
    const int threads = (int)workers.size() + 1;
    try {
        // Its own jobs first, then whatever's left.
        for (int i = self; i < count; i += threads)
            if (claimed[i].exchange(1, std::memory_order_relaxed) == 0) thunk(ctx, i);
        for (int k = 0; k < count; k++) {
            const int i = (k + self) % count;
            if (claimed[i].exchange(1, std::memory_order_relaxed) == 0) thunk(ctx, i);
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!failure) failure = std::current_exception();
    }
}

void PhysicsWorkers::runRaw(int n, Thunk fn, const void* job) {
    std::lock_guard<std::mutex> one(callMutex);
    if (serialLeft > 0) {  // (the contention guard: see the header)
        serialLeft--;
        for (int i = 0; i < n; i++) fn(job, i);
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    // (The workers are all idle here: the last batch returned only after every one finished.)
    count = n;
    thunk = fn;
    ctx = job;
    failure = nullptr;
    if (n > claimedSize) {
        claimed.reset(new std::atomic<uint8_t>[n]);
        claimedSize = n;
    }
    for (int i = 0; i < n; i++) claimed[i].store(0, std::memory_order_relaxed);
    pending.store((int)workers.size());
    generation++;
    if (sleepers.load() > 0) {
        { std::lock_guard<std::mutex> lock(mutex); }  // (so a worker about to wait can't miss this)
        start.notify_all();
    }
    process(0);  // The caller does its share too.
    const auto t1 = std::chrono::steady_clock::now();
    if (!spinUntil([&] { return pending.load() == 0; })) {
        std::unique_lock<std::mutex> lock(mutex);
        callerSleeping = true;
        done.wait(lock, [&] { return pending.load() == 0; });
        callerSleeping = false;
    }
    {  // A batch the caller mostly waited through (over 3x its own share and 0.5 ms): one more stall. Several lately: alone for a while.
        const double own = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double wait = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        stallScore = stallScore * 0.98 + (wait > 0.5 && wait > 3 * own ? 1.0 : 0.0);
        if (stallScore > 4) stallScore = 0, serialLeft = kSerialBatches, fallbackCount++;
    }
    thunk = nullptr;
    ctx = nullptr;
    if (failure) {
        std::exception_ptr e = failure;
        failure = nullptr;
        std::rethrow_exception(e);
    }
}

void PhysicsWorkers::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    start.notify_all();
    for (auto& worker : workers) worker.join();
}
PhysicsWorkers::~PhysicsWorkers() { stop(); }
