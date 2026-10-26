#include "loadprof.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>

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

namespace {
// One per thread that has marked a phase or entered a stage; a finished thread's slot is reused by the next one.
struct Slot {
    std::mutex m;
    std::string thread;
    const char* phase = nullptr;
    double phaseT = 0;
    std::vector<std::pair<const char*, double>> stack;
    bool alive = true;
    int id = 0;
};
std::mutex& slotsLock() {
    static std::mutex* m = new std::mutex;
    return *m;
}
std::vector<Slot*>& slots() {
    static std::vector<Slot*>* v = new std::vector<Slot*>;  // (never freed: the watchdog may read them while the program goes down)
    return *v;
}
struct SlotRef {
    Slot* s = nullptr;
    ~SlotRef() {
        if (!s) return;
        std::lock_guard<std::mutex> lk(s->m);
        s->alive = false, s->phase = nullptr, s->stack.clear(), s->thread.clear();
    }
};
Slot& mySlot() {
    thread_local SlotRef ref;
    if (!ref.s) {
        std::lock_guard<std::mutex> lk(slotsLock());
        for (Slot* s : slots()) {
            std::lock_guard<std::mutex> sl(s->m);
            if (!s->alive) {
                s->alive = true;
                ref.s = s;
                break;
            }
        }
        if (!ref.s) {
            ref.s = new Slot;
            ref.s->id = (int)slots().size();
            slots().push_back(ref.s);
        }
    }
    return *ref.s;
}
bool stageLog() {
    static const bool on = std::getenv("CZ_STAGE_LOG") != nullptr;
    return on;
}
double startMs() {
    static const double t = nowMs();
    return t;
}
}  // namespace

void phase(const char* name) {
    startMs();
    Slot& s = mySlot();
    std::string who;
    {
        std::lock_guard<std::mutex> lk(s.m);
        s.phase = name, s.phaseT = nowMs();
        who = s.thread;
    }
    if (name && stageLog()) {
        std::printf("stage %8.3f s %s: %s\n", (nowMs() - startMs()) / 1000, who.empty() ? "?" : who.c_str(), name);
        std::fflush(stdout);
    }
}

void nameThread(const char* name) {
    Slot& s = mySlot();
    std::lock_guard<std::mutex> lk(s.m);
    s.thread = name ? name : "";
}

void enter(const char* stage) {
    Slot& s = mySlot();
    std::lock_guard<std::mutex> lk(s.m);
    s.stack.push_back({stage, nowMs()});
}

void leave() {
    Slot& s = mySlot();
    std::lock_guard<std::mutex> lk(s.m);
    if (!s.stack.empty()) s.stack.pop_back();
}

std::string activeStages() {
    std::string out;
    const double t = nowMs();
    std::lock_guard<std::mutex> lk(slotsLock());
    for (Slot* s : slots()) {
        std::lock_guard<std::mutex> sl(s->m);
        if (!s->alive || (!s->phase && s->stack.empty())) continue;
        char buf[256];
        std::snprintf(buf, sizeof buf, "  %s #%d:", s->thread.empty() ? "thread" : s->thread.c_str(), s->id);
        out += buf;
        if (s->phase) std::snprintf(buf, sizeof buf, " %s (%.1f s)", s->phase, (t - s->phaseT) / 1000), out += buf;
        for (const auto& st : s->stack) std::snprintf(buf, sizeof buf, " > %s (%.1f s)", st.first, (t - st.second) / 1000), out += buf;
        out += "\n";
    }
    return out;
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
