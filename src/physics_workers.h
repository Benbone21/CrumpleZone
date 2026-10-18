#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Counts logical CPUs, not physical cores. 0 selects the conservative automatic budget.
int physicsThreadLimit();
int resolvePhysicsThreads(int requested, int available = physicsThreadLimit());

// Each thread takes the same jobs every batch (job i goes to thread i % threads) unless it
// runs out and has to steal: a car's nodes and beams are ~400 KB, so a car that keeps landing
// on the same core stays in that core's cache.
// Persistent workers for independent car forces. Inter-car forces stay on the caller.
// The sim hands out work 2000 times a second, so the workers spin briefly for the next
// batch instead of sleeping on a condition variable (waking a sleeper costs more than a
// car's force step); after a quiet moment, between frames, they do sleep.
class PhysicsWorkers {
public:
    explicit PhysicsWorkers(int threads);
    ~PhysicsWorkers();
    PhysicsWorkers(const PhysicsWorkers&) = delete;
    PhysicsWorkers& operator=(const PhysicsWorkers&) = delete;
    // Calls job(0..count-1), spread over the workers and the caller; returns when all are done.
    template <class F> void run(int count, const F& job) {
        runRaw(count, [](const void* f, int i) { (*static_cast<const F*>(f))(i); }, &job);
    }
private:
    typedef void (*Thunk)(const void*, int);
    void runRaw(int count, Thunk thunk, const void* job);
    std::vector<std::thread> workers;
    // The caller publishes a batch by bumping `generation`; each line below is touched by
    // different threads at different times, so they sit apart (no false sharing).
    alignas(64) std::atomic<unsigned> generation{0};
    std::unique_ptr<std::atomic<uint8_t>[]> claimed;  // per job: taken yet?
    int claimedSize = 0;
    alignas(64) std::atomic<int> pending{0};   // workers still inside the current batch
    alignas(64) std::atomic<int> sleepers{0};  // workers blocked on `start`
    std::atomic<bool> callerSleeping{false}, stopping{false};
    Thunk thunk = nullptr;
    const void* ctx = nullptr;
    int count = 0;
    std::mutex mutex;
    std::condition_variable start, done;
    std::exception_ptr failure;
    void process(int self);
    void stop();
};
