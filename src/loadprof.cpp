#include "loadprof.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace loadprof {
namespace {
std::mutex& lock() {
    static std::mutex m;
    return m;
}
std::vector<Stage>& table() {
    static std::vector<Stage> t;
    return t;
}
}  // namespace

bool enabled() {
    static const bool on = std::getenv("CZ_LOADPROF") != nullptr;
    return on;
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void add(const char* stage, double ms) {
    std::lock_guard<std::mutex> lk(lock());
    for (Stage& s : table())
        if (s.name == stage) {
            s.ms += ms, s.calls++;
            return;
        }
    table().push_back({stage, ms, 1});
}

std::vector<Stage> stages() {
    std::lock_guard<std::mutex> lk(lock());
    return table();
}

void reset() {
    std::lock_guard<std::mutex> lk(lock());
    table().clear();
}

void report(const char* title) {
    if (enabled()) {
        std::lock_guard<std::mutex> lk(lock());
        std::printf("loadprof %s:\n", title);
        for (const Stage& s : table()) std::printf("  %-28s %9.2f ms  x%d\n", s.name.c_str(), s.ms, s.calls);
        std::fflush(stdout);
    }
    reset();
}

}  // namespace loadprof
