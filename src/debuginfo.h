// F3: the debug screen's numbers, as lines of text. Nothing here draws or touches SDL/OpenGL (the test runner links
// debuginfo.cpp): the game fills a DebugInputs once a frame while the screen is up, gatherDebugInfo() turns it and
// the Sim into two columns of fixed-size lines (no allocation once they have grown), and debugscreen.cpp paints them.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "softbody.h"

class LevelRun;
class NetSession;
struct Walker;

enum DebugTone : uint8_t { DT_TEXT, DT_HEAD, DT_DIM, DT_GOOD, DT_WARN, DT_BAD };

struct DebugLine {
    char text[92];
    uint8_t tone = DT_TEXT;
};

struct DebugColumn {
    std::vector<DebugLine> lines;
    size_t used = 0;
    void clear() { used = 0; }
    // A formatted line (printf style); a blank line when fmt is "".
    void add(DebugTone tone, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    size_t size() const { return used; }
    const DebugLine& operator[](size_t i) const { return lines[i]; }
};

// The last few seconds of frame times: FPS now, the average, the 1% low (the slowest hundredth of frames), the graph.
class FrameHistory {
public:
    static constexpr int kFrames = 240;
    void push(double ms);
    void reset() { head_ = count_ = 0; }
    int count() const { return count_; }
    double ms(int back) const;  // `back` frames ago (0 = the newest)
    struct Stats {
        double fps = 0, avgFps = 0, lowFps = 0;  // now (last ~8 frames), average over the window, 1% low
        double avgMs = 0, worstMs = 0;
    };
    Stats stats() const;
private:
    float ms_[kFrames] = {};
    int head_ = 0, count_ = 0;
};

// What the game knows that the Sim doesn't. Pointers may be null (a test, a lab): those lines are left out.
struct DebugInputs {
    const Sim* sim = nullptr;
    const Walker* walker = nullptr;     // on foot when walker->active
    const LevelRun* run = nullptr;
    const NetSession* net = nullptr;
    const char* peer = "";              // the address joined (a client)
    int camMode = 0;                    // 0 chase, 1 orbit, 2 cockpit, 3 drone
    bool mph = false;
    bool playtest = false;              // trying out the editor's level
    float throttleCap = 1;
    double dayHours = 12;
    double simSeconds = 0;              // physics time since the map was loaded
    FrameHistory::Stats frame;
    double physicsMs = 0, drawCpuMs = 0;  // smoothed
    int substeps = 0;                   // physics steps taken last frame
    int physicsThreads = 1, physicsThreadLimit = 1;
    int width = 0, height = 0;
    const char* gpu = "";
    const char* build = "";             // "a1b2c3d  dev  built Oct 17 2026"
    // The renderer (zeros: none). Terrain triangles are what was drawn / the full mesh.
    unsigned terrainTris = 0, terrainFullTris = 0, terrainChunks = 0, terrainVisible = 0, sceneryVerts = 0, shadowVerts = 0;
    int propDraws = 0, propInstances = 0, propBillboards = 0;
    long long propTris = 0;
    double propGpuMs = -1;
    float renderScale = 1;
    int shadows = 0, msaa = 0;
    long rssKb = -1;                    // resident memory (-1: unknown here)
    long peakRssKb = -1;
    int biome = 0;                      // biome.h
    const char* place = "";             // the nearest named place, and how far
    double placeDist = 0;
    double rateInKB = 0, rateOutKB = 0;  // network, KB/s
};

// Both columns, cleared first.
void gatherDebugInfo(const DebugInputs& in, DebugColumn& left, DebugColumn& right);

// The process' resident memory and its peak, in KB (/proc/self/status): false where that isn't there.
bool processMemory(long& rssKb, long& peakKb);
// "NE": the compass point of a heading (degrees, 0 = north, clockwise).
const char* compassPoint(double degrees);
// The wheel's slip angle (radians, + = the tire's front pointing left of where it goes) and its coil-over travel from ride height
// (m at the spring, + compressed).
double wheelSlipAngle(const Car& car, const Wheel& wh);
double wheelSpringTravel(const Car& car, const Wheel& wh);
// What the engine makes at this rpm and throttle fill (N*m) from its torque curve.
double engineTorqueNow(const Car& car);
