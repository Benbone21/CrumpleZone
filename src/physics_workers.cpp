#include "physics_workers.h"
#include <algorithm>

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
        for (int i = 1; i < threads; i++) workers.emplace_back([this] {
            unsigned seen = 0;
            std::unique_lock<std::mutex> lock(mutex);
            for (;;) {
                start.wait(lock, [&] { return stopping || generation != seen; });
                if (stopping) return;
                seen = generation;
                lock.unlock();
                process();
                lock.lock();
                if (--pending == 0) done.notify_one();
            }
        });
    } catch (...) {
        stop();
        throw;
    }
}

void PhysicsWorkers::process() {
    try {
        for (;;) {
            const int i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= count) break;
            job(i);
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!failure) failure = std::current_exception();
    }
}

void PhysicsWorkers::run(int n, const std::function<void(int)>& fn) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        count = n;
        next.store(0, std::memory_order_relaxed);
        pending = (int)workers.size();
        job = fn;
        failure = nullptr;
        generation++;
    }
    start.notify_all();
    process();  // The main thread does its share too.
    std::unique_lock<std::mutex> lock(mutex);
    done.wait(lock, [&] { return pending == 0; });
    job = nullptr;
    if (failure) std::rethrow_exception(failure);
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
