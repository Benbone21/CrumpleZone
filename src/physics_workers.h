#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Counts logical CPUs, not physical cores. 0 selects the conservative automatic budget.
int physicsThreadLimit();
int resolvePhysicsThreads(int requested, int available = physicsThreadLimit());

// Persistent workers for independent car forces. Inter-car forces stay on the caller.
class PhysicsWorkers {
public:
    explicit PhysicsWorkers(int threads);
    ~PhysicsWorkers();
    PhysicsWorkers(const PhysicsWorkers&) = delete;
    PhysicsWorkers& operator=(const PhysicsWorkers&) = delete;
    void run(int count, const std::function<void(int)>& job);
private:
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable start, done;
    std::atomic<int> next{0};
    int count = 0, pending = 0;
    unsigned generation = 0;
    bool stopping = false;
    std::function<void(int)> job;
    std::exception_ptr failure;
    void process();
    void stop();
};
