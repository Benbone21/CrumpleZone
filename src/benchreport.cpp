#include "benchreport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>

#include "mathx.h"
#include "perf.h"
#include "platform.h"
#include "world.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <sys/utsname.h>
#endif

#ifdef _WIN32
static std::string regString(const char* key, const char* value) {
    char buf[256];
    DWORD n = sizeof buf;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buf, &n) != ERROR_SUCCESS) return "";
    return buf;
}
#elif defined(__APPLE__)
static std::string sysctlString(const char* name) {
    char buf[256];
    size_t n = sizeof buf;
    if (sysctlbyname(name, buf, &n, nullptr, 0) != 0) return "";
    return buf;
}
#endif

static std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

void queryMachine(MachineInfo& m) {
    HardwareInfo hw;
    queryHardware(hw);
    m.cores = hw.cores, m.ramMb = hw.ramMb;
#ifdef _WIN32
    m.cpu = trim(regString("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString"));
    const char* nt = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::string name = regString(nt, "ProductName");
    const int build = std::atoi(regString(nt, "CurrentBuildNumber").c_str());
    if (build >= 22000 && name.find("Windows 10") == 0) name.replace(0, 10, "Windows 11");  // (the registry still says 10)
    const std::string ver = regString(nt, "DisplayVersion");
    m.os = name + (ver.empty() ? "" : " " + ver) + " (build " + std::to_string(build) + ")";
#elif defined(__APPLE__)
    m.cpu = trim(sysctlString("machdep.cpu.brand_string"));
    if (m.cpu.empty()) m.cpu = trim(sysctlString("hw.model"));
    const std::string ver = sysctlString("kern.osproductversion"), arch = sysctlString("hw.machine");
    m.os = "macOS " + ver + (arch.empty() ? "" : " (" + arch + ")");
#else
    std::ifstream ci("/proc/cpuinfo");
    for (std::string line; std::getline(ci, line);) {
        const size_t c = line.find(':');
        if (c == std::string::npos) continue;
        const std::string k = trim(line.substr(0, c));
        if (k == "model name" || (m.cpu.empty() && (k == "Hardware" || k == "Model"))) {
            m.cpu = trim(line.substr(c + 1));
            if (k == "model name") break;
        }
    }
    std::ifstream rel("/etc/os-release");
    for (std::string line; std::getline(rel, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            m.os = line.substr(12);
            m.os.erase(std::remove(m.os.begin(), m.os.end(), '"'), m.os.end());
        }
    struct utsname u;
    if (uname(&u) == 0) m.os += (m.os.empty() ? "" : ", ") + std::string(u.sysname) + " " + u.release + " " + u.machine;
#endif
    if (m.cpu.empty()) m.cpu = "unknown";
    if (m.os.empty()) m.os = "unknown";
}

std::string benchTimestamp() {
    const std::time_t t = std::time(nullptr);
    char b[32];
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tmv);
    return b;
}

std::string benchSlug(const std::string& in) {
    std::string s = in;
    for (char& c : s) c = std::isalnum((unsigned char)c) || c == '-' ? (char)std::tolower((unsigned char)c) : '_';
    return s;
}

std::string benchStem(const std::string& stamp, const std::string& map, const std::string& preset, int w, int h) {
    return benchSlug(stamp + "-" + map + "-" + preset + "-" + std::to_string(w) + "x" + std::to_string(h));
}

std::string benchDir() {
    const std::string d = configDir() + "/bench";
    for (size_t i = 1; i <= d.size(); i++)  // (every folder on the way: a fresh machine has no config folder yet)
        if (i == d.size() || d[i] == '/' || d[i] == '\\') makeDir(d.substr(0, i));
    return d;
}

static std::string machineText(const MachineInfo& m) {
    char b[160];
    std::string out;
    out += "GPU:       " + m.gpu + "\n";
    out += "Vendor:    " + m.vendor + "\n";
    out += "GL:        " + m.glVersion + (m.glsl.empty() ? "" : " (GLSL " + m.glsl + ")") + "\n";
    out += "CPU:       " + m.cpu + "\n";
    std::snprintf(b, sizeof b, "Cores:     %d logical\nRAM:       %ld MB\n", m.cores, m.ramMb);
    out += b;
    out += "OS:        " + m.os + "\n";
    std::snprintf(b, sizeof b, "Display:   %s, refresh %.0f Hz\n", m.display.empty() ? "unknown" : m.display.c_str(), m.refreshHz);
    out += b;
    out += "Video:     " + (m.videoDriver.empty() ? std::string("unknown") : m.videoDriver) + "\n";
    return out;
}

std::string benchFileText(const MachineInfo& m, const BenchRunInfo& run, const BenchStats::Result& r) {
    std::string out = "Crumple Zone benchmark report\n\n" + machineText(m);
    char b[256];
    out += "\nRun:       " + run.map + (run.place.empty() ? "" : " / " + run.place) + ", quality " + run.preset + "\n";
    std::snprintf(b, sizeof b, "Window:    %dx%d (drawable %dx%d)\nFrames:    %d after %d to warm up%s\n", run.width, run.height, run.drawW, run.drawH, run.frames,
                  run.warm, run.synced ? ", GPU synced per stage" : "");
    out += b;
    out += "Settings:  " + run.settings + "\n\n";
    out += BenchStats::report(r, run.map + ", " + run.preset);
    return out;
}

bool writeTextFile(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool writeBMP(const std::string& path, const std::vector<unsigned char>& rgb, int w, int h) {
    if (w <= 0 || h <= 0 || rgb.size() < (size_t)w * h * 3) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const int row = (w * 3 + 3) & ~3;
    const uint32_t size = 54 + (uint32_t)row * h;
    unsigned char hd[54] = {'B', 'M'};
    auto put = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) hd[at + i] = (unsigned char)(v >> (8 * i)); };
    put(2, size), put(10, 54), put(14, 40), put(18, (uint32_t)w), put(22, (uint32_t)h);
    hd[26] = 1, hd[28] = 24;
    put(34, (uint32_t)row * h);
    std::fwrite(hd, 1, 54, f);
    std::vector<unsigned char> line(row, 0);
    for (int y = 0; y < h; y++) {  // (BMP rows are bottom-up too, like GL's)
        const unsigned char* src = &rgb[(size_t)y * w * 3];
        for (int x = 0; x < w; x++) line[x * 3] = src[x * 3 + 2], line[x * 3 + 1] = src[x * 3 + 1], line[x * 3 + 2] = src[x * 3];
        std::fwrite(line.data(), 1, row, f);
    }
    return std::fclose(f) == 0;
}

BenchSuite::BenchSuite() {
    static const int presets[] = {Q_POTATO, Q_LOW, Q_MEDIUM, Q_HIGH};
    Entry gully;  // Boulder Gully: the mountain, the car at the foot of the ravine facing up it
    const V3 foot = mountainFeature("gully"), top = mountainFeature("gullytop");
    gully.map = MAP_MOUNTAIN, gully.place = "Boulder Gully", gully.spawn = true;
    gully.x = foot.x, gully.z = foot.z, gully.heading = std::atan2(top.z - foot.z, top.x - foot.x) * 180 / M_PI;
    for (int map : {(int)MAP_FRONTIER, (int)MAP_WILDS, -1})
        for (int p : presets) {
            Entry e = map < 0 ? gully : Entry();
            if (map >= 0) e.map = map;
            e.preset = p;
            plan_.push_back(e);
        }
}

bool BenchSuite::next() {
    stats_ = BenchStats();
    idx_++;
    return idx_ < size();
}

void BenchSuite::record(const std::string& mapName, const std::string& presetName) {
    Done d;
    d.e = plan_[idx_], d.mapName = mapName, d.presetName = presetName;
    d.r = stats_.result();
    d.csv = BenchStats::csvLine(d.r);
    done_.push_back(d);
}

std::string BenchSuite::summary(const MachineInfo& m, const std::string& stamp, int w, int h, int frames, int warm) const {
    char b[320];
    std::string out = "Crumple Zone benchmark suite " + stamp + "\n\n" + machineText(m);
    std::snprintf(b, sizeof b, "Window:    %dx%d, %d frames per run after %d to warm up, %d runs\n\n", w, h, frames, warm, (int)done_.size());
    out += b;
    out += "Map / place                  quality   avg ms   fps  1% low ms  hitches  >30ms\n";
    for (const Done& d : done_) {
        const std::string name = d.mapName + (*d.e.place ? std::string(" / ") + d.e.place : "");
        std::snprintf(b, sizeof b, "%-28s %-8s %7.2f %5.1f %10.2f %8d %6d\n", name.c_str(), d.presetName.c_str(), d.r.avgMs, d.r.avgFps, d.r.lowMs, d.r.hitches,
                      d.r.over30);
        out += b;
    }
    out += "\nCSV (map,place,quality,then bench-csv fields: STAGE=cpu/gpu ms):\n";
    for (const Done& d : done_) out += "suite-csv: " + d.mapName + "," + d.e.place + "," + d.presetName + "," + d.csv + "\n";
    out += "\nPer-run stage tables:\n";
    for (const Done& d : done_) out += "\n" + BenchStats::report(d.r, d.mapName + (*d.e.place ? std::string(" / ") + d.e.place : "") + ", " + d.presetName);
    return out;
}
