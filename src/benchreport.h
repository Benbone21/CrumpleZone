// What --bench and --bench-suite write to disk (the Windows build has no console): the machine, the settings, the numbers.
// No SDL or GL in here (main.cpp hands in the GL strings and the display); the BenchSuite is the plan and the per-run bookkeeping.
#pragma once
#include <string>
#include <vector>

#include "frameprof.h"

struct MachineInfo {
    std::string gpu, vendor, glVersion, glsl, videoDriver;  // GL_RENDERER, GL_VENDOR, GL_VERSION (the driver build is in it), GLSL, SDL's video driver
    std::string cpu, os;
    int cores = 0;           // logical
    long ramMb = 0;
    double refreshHz = 0;    // the window's display
    std::string display;     // "name WxH @ Hz"
};
void queryMachine(MachineInfo& m);  // fills cpu, os, cores, ramMb (Windows registry, macOS sysctl, Linux /proc)

// One run's identity and settings, for the report.
struct BenchRunInfo {
    std::string map, preset;     // "FRONTIER", "MEDIUM"
    std::string place;           // where on the map ("" for the start, "Boulder Gully")
    int width = 0, height = 0;   // the window; drawW/H the drawable (differs on a hi-dpi screen)
    int drawW = 0, drawH = 0;
    int frames = 0, warm = 0;
    bool synced = false;
    std::string settings;        // "scale 1.00, shadows 3, ..." (one line)
};

// "<date-time>-<map>-<quality>-<WxH>" with nothing a file name dislikes; date-time "20261017-142530" (local time).
std::string benchTimestamp();
std::string benchStem(const std::string& stamp, const std::string& map, const std::string& preset, int w, int h);
std::string benchSlug(const std::string& s);  // lower case, anything but letters, digits and - becomes _
std::string benchDir();  // configDir()/bench, made

// The whole report: machine, settings, the BenchStats lines (stage table and the "bench-csv:" line).
std::string benchFileText(const MachineInfo& m, const BenchRunInfo& run, const BenchStats::Result& r);

bool writeTextFile(const std::string& path, const std::string& text);
// A 24-bit BMP (opens everywhere) from GL's bottom-up RGB rows.
bool writeBMP(const std::string& path, const std::vector<unsigned char>& rgb, int w, int h);

// --bench-suite: the fixed set of runs, back to back in one launch.
class BenchSuite {
public:
    struct Entry {
        int map = 0, preset = 0;
        const char* place = "";  // "" or a spot on the map
        bool spawn = false;      // start the car here (x, z, heading in degrees) instead of the map's start
        double x = 0, z = 0, heading = 0;
    };
    struct Done {
        Entry e;
        std::string mapName, presetName;
        BenchStats::Result r;
        std::string csv;         // BenchStats::csvLine
    };
    BenchSuite();                       // the plan: Frontier, Wilds (OPEN COUNTRY) and Boulder Gully, each at POTATO, LOW, MEDIUM, HIGH
    explicit BenchSuite(std::vector<Entry> plan) : plan_(std::move(plan)) {}
    int size() const { return (int)plan_.size(); }
    int index() const { return idx_; }            // the run under way (-1 before the first)
    bool begun() const { return idx_ >= 0; }
    bool finished() const { return idx_ >= size(); }
    const Entry& current() const { return plan_[idx_]; }
    // Starts the next run with an empty BenchStats (nothing carries over). False when that was the last.
    bool next();
    BenchStats& stats() { return stats_; }
    // Files the current run's numbers (names from main.cpp's mapName/presetName).
    void record(const std::string& mapName, const std::string& presetName);
    const std::vector<Done>& done() const { return done_; }
    // The summary file: the machine, a table, one csv line per run, then each run's stage table.
    std::string summary(const MachineInfo& m, const std::string& stamp, int w, int h, int frames, int warm) const;

private:
    std::vector<Entry> plan_;
    std::vector<Done> done_;
    BenchStats stats_;
    int idx_ = -1;
};
