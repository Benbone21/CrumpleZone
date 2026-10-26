// One background thread for work the frame shouldn't wait on: the renderer's streaming (the terrain texture's window as the
// camera moves, the terrain chunks' meshes coming into range). Jobs run in the order posted, one at a time, on a thread
// below the frame thread's priority, so on a busy machine it's the streaming that waits, not the frame.
// Jobs that read the World run on one made with readsWorld: whoever replaces the world waits for them all first
// (waitWorldReaders(); Renderer::waitStreaming does).
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class BgWork {
public:
    explicit BgWork(bool readsWorld = false);
    ~BgWork();
    BgWork(const BgWork&) = delete;
    BgWork& operator=(const BgWork&) = delete;
    void post(std::function<void()> job);  // (the thread starts with the first job)
    void wait();                           // every job posted so far has run
    bool idle();                           // nothing queued or running
private:
    void run();
    std::mutex m_;
    std::condition_variable wake_, idle_;
    std::deque<std::function<void()>> q_;
    bool running_ = false, quit_ = false, readsWorld_ = false;
    std::thread t_;
};

// Every job posted so far, on every BgWork made with readsWorld, has run.
void waitWorldReaders();

// The calling thread's scheduling priority lowered a step (Linux: nice +5; Windows: below normal; macOS: the utility QoS).
void lowerThreadPriority();
