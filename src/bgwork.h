// One background thread for work the frame shouldn't wait on: the renderer's streaming (the terrain texture's window as the
// camera moves, the terrain chunks' meshes coming into range). Jobs run in the order posted, one at a time, on a thread
// below the frame thread's priority, so on a busy machine it's the streaming that waits, not the frame.
// The jobs read the World: whoever replaces or edits the world waits for them first (wait()).
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class BgWork {
public:
    BgWork() = default;
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
    bool running_ = false, quit_ = false;
    std::thread t_;
};

// The calling thread's scheduling priority lowered a step (Linux: nice +5; Windows: below normal; macOS: the utility QoS).
void lowerThreadPriority();
