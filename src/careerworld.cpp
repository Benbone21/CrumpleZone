#include "careerworld.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "net.h"
#include "platform.h"
#include "vehicles.h"

const char* difficultyName(Difficulty d) {
    switch (d) {
        case DIFF_LIGHT: return "LIGHT";
        case DIFF_MEDIUM: return "MEDIUM";
        case DIFF_HARDCORE: return "HARDCORE";
        default: return "MEDIUM";
    }
}

const char* modeName(WorldMode m) { return m == MODE_CREW ? "SHARED CREW" : "PERSONAL"; }

int startCash(Difficulty d) {
    switch (d) {
        case DIFF_LIGHT: return 2000;
        case DIFF_HARDCORE: return 500;
        default: return 1000;
    }
}

bool startableVehicle(int catalogIndex) {
    const auto& cat = vehicleCatalog();
    if (catalogIndex < 0 || catalogIndex >= (int)cat.size()) return false;
    return std::string(cat[catalogIndex].name) != "KOENIGSEGG JESKO";
}

std::string worldSlug(const std::string& name, const std::vector<std::string>& taken) {
    const size_t kMax = 32;
    std::string base;
    bool dash = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (dash && !base.empty()) base += '-';
            dash = false;
            base += (char)std::tolower(c);
        } else {
            dash = true;
        }
    }
    if (base.size() > kMax) base.resize(kMax);
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "world";
    auto isTaken = [&](const std::string& s) { return std::find(taken.begin(), taken.end(), s) != taken.end(); };
    std::string slug = base;
    for (int n = 2; isTaken(slug); n++) {
        std::string suffix = "-" + std::to_string(n);
        slug = base.substr(0, kMax - suffix.size()) + suffix;
    }
    return slug;
}

namespace {
thread_local std::string tCareerDir;
}

std::string careerConfigDir() { return tCareerDir.empty() ? configDir() : tCareerDir; }
ScopedCareerDir::ScopedCareerDir(std::string dir) : prev_(tCareerDir) { tCareerDir = std::move(dir); }
ScopedCareerDir::~ScopedCareerDir() { tCareerDir = prev_; }

namespace {

// Text fields are one line in the file: backslash and newline are escaped.
std::string escapeLine(const std::string& s) {
    std::string r;
    for (char c : s) r += c == '\\' ? "\\\\" : c == '\n' ? "\\n" : c == '\r' ? "" : std::string(1, c);
    return r;
}
std::string unescapeLine(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) r += s[++i] == 'n' ? '\n' : s[i];
        else r += s[i];
    }
    return r;
}

// Reads world text; false if it has no "name" line or no closing "end 1" line (not a world file, or cut short). Keys it doesn't know are skipped.
bool parseWorld(const std::string& text, WorldSave& w) {
    const std::string slug = w.slug;
    w = WorldSave();
    w.slug = slug;
    bool sawName = false, sawEnd = false;
    std::vector<std::pair<size_t, std::string>> conds;
    std::istringstream f(text);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "end 1") sawEnd = true;  // (the last line: a file cut short has none)
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string k = line.substr(0, sp), v = line.substr(sp + 1);
        if (k == "name") w.name = unescapeLine(v), sawName = true;
        else if (k == "map") w.map = std::atoi(v.c_str());
        else if (k == "diff") w.diff = (Difficulty)std::min(std::max(std::atoi(v.c_str()), 0), (int)DIFF_COUNT - 1);
        else if (k == "mode") w.mode = std::atoi(v.c_str()) == MODE_CREW ? MODE_CREW : MODE_PERSONAL;
        else if (k == "build") w.build = v;
        else if (k == "played") w.playedSeconds = std::max(0.0, std::atof(v.c_str()));
        else if (k == "cache") w.opened.insert(v);
        else if (k == "left") {  // "left VEHICLE OWNER"
            const size_t sp2 = v.find(' ');
            LeftVehicle lv;
            lv.vehicle = std::atoi(v.substr(0, sp2).c_str());
            if (sp2 != std::string::npos) lv.owner = unescapeLine(v.substr(sp2 + 1));
            w.left.push_back(lv);
        } else if (k == "lcond") {  // "lcond N TEXT"
            const size_t sp2 = v.find(' ');
            const int n = std::atoi(v.substr(0, sp2).c_str());
            if (n >= 0) conds.push_back({(size_t)n, sp2 == std::string::npos ? "" : unescapeLine(v.substr(sp2 + 1))});
        }
    }
    for (const auto& c : conds)
        if (c.first < w.left.size()) w.left[c.first].condition = c.second;
    return sawName && sawEnd;
}

bool validWorldText(const std::string& text) {
    WorldSave probe;
    return parseWorld(text, probe);
}

std::string worldText(const WorldSave& w) {
    std::string t = "name " + escapeLine(w.name) + '\n';
    t += "map " + std::to_string(w.map) + '\n';
    t += "diff " + std::to_string((int)w.diff) + '\n';
    t += "mode " + std::to_string((int)w.mode) + '\n';
    if (!w.build.empty()) t += "build " + w.build + '\n';
    char buf[48];
    std::snprintf(buf, sizeof buf, "played %.3f\n", w.playedSeconds);
    t += buf;
    for (const auto& k : w.opened) t += "cache " + k + '\n';
    for (size_t i = 0; i < w.left.size(); i++) {
        t += "left " + std::to_string(w.left[i].vehicle) + ' ' + escapeLine(w.left[i].owner) + '\n';
        if (!w.left[i].condition.empty()) t += "lcond " + std::to_string(i) + ' ' + escapeLine(w.left[i].condition) + '\n';
    }
    return t + "end 1\n";
}

bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// A slug is a folder name we made: nothing that could leave worldsDir().
bool safeSlug(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
    return true;
}

}  // namespace

std::string worldsDir() { return careerConfigDir() + "/worlds"; }

bool saveWorld(const WorldSave& w) {
    if (!safeSlug(w.slug)) return false;
    makeDir(careerConfigDir());
    makeDir(worldsDir());
    makeDir(worldsDir() + "/" + w.slug);
    std::string err;
    return writeFileAtomic(worldsDir() + "/" + w.slug + "/world.txt", worldText(w), &err, true, validWorldText);
}

bool loadWorld(const std::string& slug, WorldSave& w) {
    if (safeSlug(slug)) {
        const std::string path = worldsDir() + "/" + slug + "/world.txt";
        for (const std::string& p : {path, path + ".bak"}) {
            std::string text;
            WorldSave got;
            got.slug = slug;
            if (readAll(p, text) && parseWorld(text, got)) {
                w = got;
                return true;
            }
        }
    }
    w = WorldSave();
    w.slug = slug;
    w.name = slug;
    w.damaged = true;
    return false;
}

std::vector<WorldSave> listWorlds() {
    std::vector<std::pair<long long, WorldSave>> found;
    for (const std::string& n : listDir(worldsDir())) {
        if (!isDir(worldsDir() + "/" + n)) continue;
        WorldSave w;
        loadWorld(n, w);
        found.push_back({fileMTime(worldsDir() + "/" + n + "/world.txt"), w});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second.slug < b.second.slug;
    });
    std::vector<WorldSave> out;
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
}

bool deleteWorld(const std::string& slug) {
    if (!safeSlug(slug)) return false;
    return removeTree(worldsDir() + "/" + slug);
}

WorldSave newWorld(const std::string& name, int map, Difficulty d, WorldMode m, const std::vector<WorldSave>& existing) {
    std::vector<std::string> taken;
    for (const auto& e : existing) taken.push_back(e.slug);
    WorldSave w;
    w.name = name;
    w.slug = worldSlug(name, taken);
    w.map = map;
    w.diff = d;
    w.mode = m;
    w.build = net::kRelease;
    return w;
}
