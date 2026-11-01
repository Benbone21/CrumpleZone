// Load-time profile: how long each stage of startup and of a map load takes, and the worst frame (hitch) of a run.
// Stages add up from any thread (a scope per stage); CZ_LOADPROF=1 prints them. Nothing here touches OpenGL.
#pragma once
#include <string>
#include <vector>

namespace loadprof {

struct Stage {
    std::string name;
    double ms = 0;  // total over all calls (on every thread)
    int calls = 0;
};

bool enabled();                        // CZ_LOADPROF is set
void add(const char* stage, double ms);
std::vector<Stage> stages();           // in first-seen order
void reset();
void report(const char* title);        // prints the stages (if enabled), then clears them
double nowMs();

// What every thread is busy with right now, for a hang report (the watchdog prints it when it trips). Each thread has a
// phase (a step of startup: phase("sdl.window") replaces the one before; nullptr: none) and the stack of the Scopes and
// Steps it is inside. Names must outlive their use (string literals). CZ_STAGE_LOG=1 prints each phase as it starts.
void phase(const char* name);
void nameThread(const char* name);  // shown in the report ("main", "car visual", ...)
void enter(const char* stage);      // (Scope and Steps do these)
void leave();
std::string activeStages();         // one line per busy thread: "  main #0: sdl.window (912.3 s) > ..."; "" if none

struct Scope {
    const char* name;
    double t0;
    explicit Scope(const char* n) : name(n), t0(nowMs()) { enter(n); }
    ~Scope() { leave(), add(name, nowMs() - t0); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// Back-to-back stages of one job (a map's build): next("b") ends the stage before it and starts b; the last ends with it.
struct Steps {
    const char* name = nullptr;
    double t0 = 0;
    void next(const char* n) {
        const double t = nowMs();
        if (name) leave(), add(name, t - t0);
        name = n, t0 = t;
        if (n) enter(n);
    }
    Steps() = default;
    explicit Steps(const char* first) { next(first); }
    ~Steps() { next(nullptr); }
    Steps(const Steps&) = delete;
    Steps& operator=(const Steps&) = delete;
};

// Frame times of a run: the worst one is the hitch.
struct Frames {
    double worst = 0, sum = 0;
    int n = 0;
    void add(double ms) { worst = ms > worst ? ms : worst, sum += ms, n++; }
    void reset() { *this = Frames(); }
    double mean() const { return n ? sum / n : 0; }
};

}  // namespace loadprof

#define LOADPROF_CAT2(a, b) a##b
#define LOADPROF_CAT(a, b) LOADPROF_CAT2(a, b)
#define LOADPROF(name) loadprof::Scope LOADPROF_CAT(loadprofScope_, __LINE__)(name)
