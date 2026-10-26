#include "bgwork.h"

#include <cerrno>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

void lowerThreadPriority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
    // (Per thread on Linux: a thread id is a process id to setpriority. Lowering needs no privilege.)
    const int tid = (int)syscall(SYS_gettid);
    errno = 0;
    const int now = getpriority(PRIO_PROCESS, (id_t)tid);
    if (errno == 0) setpriority(PRIO_PROCESS, (id_t)tid, now + 5 > 19 ? 19 : now + 5);
#endif
}

BgWork::~BgWork() {
    {
        std::lock_guard<std::mutex> l(m_);
        quit_ = true;
    }
    wake_.notify_all();
    if (t_.joinable()) t_.join();
}

void BgWork::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> l(m_);
        q_.push_back(std::move(job));
        if (!t_.joinable()) t_ = std::thread([this] { run(); });
    }
    wake_.notify_one();
}

void BgWork::wait() {
    std::unique_lock<std::mutex> l(m_);
    idle_.wait(l, [&] { return q_.empty() && !running_; });
}

bool BgWork::idle() {
    std::lock_guard<std::mutex> l(m_);
    return q_.empty() && !running_;
}

void BgWork::run() {
    lowerThreadPriority();
    std::unique_lock<std::mutex> l(m_);
    for (;;) {
        wake_.wait(l, [&] { return quit_ || !q_.empty(); });
        if (q_.empty()) return;  // (quitting, nothing left)
        std::function<void()> job = std::move(q_.front());
        q_.pop_front();
        running_ = true;
        l.unlock();
        job();
        l.lock();
        running_ = false;
        if (q_.empty()) idle_.notify_all();
    }
}
