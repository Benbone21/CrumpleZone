// Hang watchdogs for unattended runs (portable: std::thread and std::chrono only; the thread dump reads /proc on Linux and runs sample(1) on macOS).
//   wd::dumpThreads - every thread's kernel wait channel (/proc/self/task/*/wchan): "futex_do_wait" everywhere is a deadlock.
//   wd::Watchdog    - a thread that polls check() and trips once it returns a message. The default trip prints it, dumps the
//                     threads and _exit()s, since a hung thread can't be joined. Tests give it their own onTrip.
//   wd::FrameWatchdog - the game's, for scripted runs: trips when no frame completes for a while or the run goes over its limit.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#ifdef __linux__
#include <dirent.h>
#endif

namespace wd {

// What each thread was doing (the game sets it to loadprof::activeStages): printed when a FrameWatchdog trips.
inline std::function<std::string()> stageReport;

inline void dumpThreads() {
#ifdef __APPLE__
    // No /proc: macOS's sample(1) prints every thread's stack (a second of samples), which names the call that hangs.
    std::fflush(stdout);
    char cmd[96];
    std::snprintf(cmd, sizeof cmd, "/usr/bin/sample %d 1 -mayDie -file /dev/stdout 2>&1", (int)getpid());
    if (std::system(cmd) != 0) std::printf("  (sample failed)\n");
#endif
#ifdef __linux__
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string base = std::string("/proc/self/task/") + e->d_name, name, wchan;
            char buf[128];
            for (int k = 0; k < 2; k++)
                if (std::FILE* f = std::fopen((base + (k ? "/wchan" : "/comm")).c_str(), "r")) {
                    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
                    buf[n] = 0;
                    for (char* p = buf; *p; p++) if (*p == '\n') *p = 0;
                    (k ? wchan : name) = buf;
                    std::fclose(f);
                }
            std::printf("  thread %s (%s): %s\n", e->d_name, name.c_str(), wchan.empty() ? "?" : wchan.c_str());
        }
        closedir(d);
    }
#endif
    std::fflush(stdout);
}

[[noreturn]] inline void hardExit(int code) {
    std::fflush(stdout);
    std::fflush(stderr);
    _exit(code);
}

class Watchdog {
public:
    // check() runs every `pollMs` on the watchdog thread; a non-empty return trips it (once).
    Watchdog(std::function<std::string()> check, std::function<void(const std::string&)> onTrip, int pollMs = 200)
        : check_(std::move(check)), onTrip_(std::move(onTrip)), pollMs_(pollMs), th_([this] { loop(); }) {}
    ~Watchdog() {
        {
            std::lock_guard<std::mutex> l(m_);
            stop_ = true;
        }
        cv_.notify_all();
        th_.join();
    }
    bool tripped() const { return tripped_; }

private:
    void loop() {
        std::unique_lock<std::mutex> l(m_);
        while (!stop_) {
            cv_.wait_for(l, std::chrono::milliseconds(pollMs_), [&] { return stop_; });
            if (stop_) break;
            l.unlock();
            std::string msg = check_();
            l.lock();
            if (!msg.empty() && !stop_) {
                tripped_ = true;
                l.unlock();
                onTrip_(msg);
                return;
            }
        }
    }
    std::function<std::string()> check_;
    std::function<void(const std::string&)> onTrip_;
    int pollMs_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::atomic<bool> tripped_{false};
    std::thread th_;  // (last: starts after the rest is built)
};

inline double nowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Scripted game runs. frame() once per completed frame; stalls count from the last one (from construction before the first:
// loading takes a while, so that gets `loadSec`).
class FrameWatchdog {
public:
    FrameWatchdog(double stallSec, double totalSec, double loadSec,
                  std::function<void(const std::string&)> onTrip = nullptr)
        : stall_(stallSec), total_(totalSec), load_(loadSec), start_(nowSec()), last_(start_),
          dog_([this] { return check(); }, onTrip ? std::move(onTrip) : [](const std::string& m) {
              std::printf("%s\n", m.c_str());
              if (stageReport) {
                  const std::string s = stageReport();
                  std::printf("what each thread was doing (loading stages):\n%s", s.empty() ? "  (no stage marked)\n" : s.c_str());
              }
              dumpThreads();
              hardExit(3);
          }) {}
    void frame(int n) {
        frame_ = n;
        last_ = nowSec();
        started_ = true;
    }
    bool tripped() const { return dog_.tripped(); }

private:
    std::string check() {
        const double t = nowSec();
        char buf[160];
        if (t - start_ > total_) {
            std::snprintf(buf, sizeof(buf), "HANG: run over its %.0f s limit at frame %d", total_, (int)frame_);
            return buf;
        }
        const double lim = started_ ? stall_ : load_;
        if (t - last_ > lim) {
            if (started_) std::snprintf(buf, sizeof(buf), "HANG: no frame for %.0f s at frame %d", stall_, (int)frame_);
            else std::snprintf(buf, sizeof(buf), "HANG: no frame for %.0f s while loading (before the first frame)", load_);
            return buf;
        }
        return "";
    }
    double stall_, total_, load_, start_;
    std::atomic<double> last_;
    std::atomic<int> frame_{0};
    std::atomic<bool> started_{false};
    Watchdog dog_;
};

}  // namespace wd
