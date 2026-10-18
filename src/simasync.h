// Runs a Sim::advance on its own thread while the main thread waits on the GPU (the swap):
// the physics leaves the frame's critical path. The caller owns the rule that nothing else
// touches the Sim between kick() and join().
#pragma once
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include "softbody.h"

class SimAsync {
public:
    SimAsync() : thread([this] { loop(); }) {}
    ~SimAsync() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quit = true;
        }
        wake.notify_all();
        thread.join();
    }
    SimAsync(const SimAsync&) = delete;
    SimAsync& operator=(const SimAsync&) = delete;
    void kick(Sim& s, double dt, int maxSteps) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            sim = &s, realDt = dt, limit = maxSteps, steps = 0, busy = true, failure = nullptr;
        }
        wake.notify_all();
    }
    // Waits for the kicked advance; returns the substeps it ran (0 if none was kicked).
    int join() {
        std::unique_lock<std::mutex> lock(mutex);
        finished.wait(lock, [this] { return !busy; });
        if (failure) {
            std::exception_ptr e = failure;
            failure = nullptr;
            std::rethrow_exception(e);
        }
        const int n = steps;
        steps = 0;
        return n;
    }
    bool running() {
        std::lock_guard<std::mutex> lock(mutex);
        return busy;
    }
private:
    void loop() {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;) {
            wake.wait(lock, [this] { return busy || quit; });
            if (quit) return;
            Sim* s = sim;
            const double dt = realDt;
            const int max = limit;
            lock.unlock();
            int n = 0;
            std::exception_ptr err;
            try { n = s->advance(dt, max); } catch (...) { err = std::current_exception(); }
            lock.lock();
            steps = n, failure = err, busy = false;
            finished.notify_all();
        }
    }
    std::mutex mutex;
    std::condition_variable wake, finished;
    Sim* sim = nullptr;
    double realDt = 0;
    int limit = 0, steps = 0;
    bool busy = false, quit = false;
    std::exception_ptr failure;
    std::thread thread;  // (last: it starts using the members above)
};
