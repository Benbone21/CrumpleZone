#include "level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "platform.h"
#include "world.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif


const char* levelModeName(int mode) {
    static const char* names[LEVEL_MODE_COUNT] = {"FREE DRIVE", "RACE", "SPRINT"};
    return mode >= 0 && mode < LEVEL_MODE_COUNT ? names[mode] : "?";
}

namespace {
const char* kKindNames[SK_COUNT] = {"LEDGES", "CLIMB", "BOULDERS", "BOG", "CROSSING", "SHELF", "TWISTER", "TIMBER", "DUNES", "DROP", "GAP", "CUSTOM"};
const char* kTestNames[ST_COUNT] = {"SKILL", "PROBLEM-SOLVING", "COMMITMENT", "ATTRITION", "CO-OP"};
const char* kKitNames[SKIT_COUNT] = {"4L", "LOCKERS", "AIR DOWN", "WINCH", "BUDDY"};
// (In the file: one word each.)
const char* kTestTokens[ST_COUNT] = {"SKILL", "PROBLEM", "COMMIT", "ATTRITION", "COOP"};
const char* kKitTokens[SKIT_COUNT] = {"4L", "LOCKERS", "AIRDOWN", "WINCH", "BUDDY"};
}  // namespace

const char* sectionKindName(int kind) { return kind >= 0 && kind < SK_COUNT ? kKindNames[kind] : "?"; }
const char* sectionTestName(int bit) { return bit >= 0 && bit < ST_COUNT ? kTestNames[bit] : "?"; }
const char* sectionKitName(int bit) { return bit >= 0 && bit < SKIT_COUNT ? kKitNames[bit] : "?"; }

std::string sectionBitsText(int bits, bool kit) {
    std::string r;
    for (int k = 0; k < 5; k++)
        if (bits >> k & 1) r += (r.empty() ? "" : ",") + std::string(kit ? kKitTokens[k] : kTestTokens[k]);
    return r.empty() ? std::string("-") : r;
}

int sectionBitsParse(const std::string& list, bool kit) {
    int bits = 0;
    std::istringstream ts(list);
    std::string t;
    while (std::getline(ts, t, ','))
        for (int k = 0; k < 5; k++)
            if (t == (kit ? kKitTokens[k] : kTestTokens[k])) bits |= 1 << k;
    return bits;
}

// ---- Applying ----------------------------------------------------------------------------

void placeObject(const LevelObject& o, World& w) {
    switch (o.kind) {
        case OBJ_ROCK: {
            Rock& r = w.addRock(o.p.x, o.p.z, o.size, o.top, o.seed, o.col[0]);
            (void)r;
            break;
        }
        case OBJ_BOX:
            w.addBox(o.p, o.size, o.yaw, o.pitch, o.roll, V3(o.col[0], o.col[1], o.col[2]));
            w.boxes.back().rock = o.sub == 1;
            break;
        case OBJ_LOG: w.addLog(o.p, o.p2, o.size.x); break;
        case OBJ_TREE: w.addTree(o.p.x, o.p.z, std::max(1.0, o.size.y), o.sub, o.col[0]); break;
        case OBJ_STATION: {
            // (The station keeps a pointer to its name: levels' names live as long as the game.)
            static std::vector<std::string*> names;
            names.push_back(new std::string(o.name.empty() ? "REPAIR SHOP" : o.name));
            w.addStation(V3(o.p.x, w.height(o.p.x, o.p.z), o.p.z), o.yaw, names.back()->c_str());
            break;
        }
        default: break;
    }
}

void applyLevel(const Level& lv, World& w) {
    for (const V3& r : lv.removed) w.removeNear(r.x, r.z, 1.5);
    const int n = w.gridSize();
    for (const auto& h : lv.heights)
        if (h.first >= 0 && h.first < n * n) w.setCellHeight(h.first, h.second);
    for (const auto& s : lv.surfaces)
        if (s.first >= 0 && s.first < n * n) w.setCellSurface(s.first, (uint8_t)(s.second >> 8), (uint8_t)(s.second & 0xff));
    for (const auto& wt : lv.water)
        if (wt.first >= 0 && wt.first < n * n) w.setCellWater(wt.first, wt.second);
    for (const auto& f : lv.flow)
        if (f.first >= 0 && f.first < n * n && !w.flowX.empty()) w.flowX[f.first] = f.second.first, w.flowZ[f.first] = f.second.second;
    for (const LevelObject& o : lv.objects) placeObject(o, w);
    if (lv.section.on) {  // (a section's canvas is cleared: the bushes have no entry in `removed`)
        const SectionInfo& c = lv.section;
        w.bushes.erase(std::remove_if(w.bushes.begin(), w.bushes.end(), [&](const Bush& b) { return c.outside(b.p.x, b.p.z) < 26; }), w.bushes.end());
    }
    if (lv.hasStart) {
        w.spawn = V3(lv.start.x, w.height(lv.start.x, lv.start.z), lv.start.z);
        w.spawnYaw = lv.startYaw;
    }
    w.buildGrid();
}

void addGateMarkers(const Level& lv, World& w) {
    const int n = (int)lv.gates.size();
    for (int k = 0; k < n; k++) {
        const Gate& g = lv.gates[k];
        const V3 f(std::cos(g.yaw), 0, -std::sin(g.yaw)), l(-f.z, 0, f.x);
        const bool line = k == n - 1;  // the finish (or the start-finish line)
        const size_t first = w.boxes.size();
        const V3 post = line ? V3(0.95, 0.95, 0.95) : V3(0.95, 0.45, 0.1);
        double top = -1e9;
        for (int side = -1; side <= 1; side += 2) {
            const V3 p = g.p + l * (side * g.width / 2);
            const double y = w.height(p.x, p.z);
            top = std::max(top, y + 4.2);
            w.addBox(V3(p.x, y + 2.1, p.z), V3(0.14, 2.1, 0.14), g.yaw, 0, 0, post);
            w.boxes.back().solid = false;
        }
        // The banner across: checkered at the line (alternating blocks), orange otherwise.
        const int blocks = line ? std::max(4, (int)(g.width / 1.2)) : 1;
        for (int b = 0; b < blocks; b++) {
            const double a = -g.width / 2 + g.width * (b + 0.5) / blocks;
            const V3 c = g.p + l * a;
            const V3 col = line ? (b % 2 ? V3(0.05, 0.05, 0.05) : V3(0.95, 0.95, 0.95)) : V3(0.98, 0.55, 0.12);
            w.addBox(V3(c.x, top - 0.35, c.z), V3(0.08, 0.35, g.width / blocks / 2), g.yaw, 0, 0, col);
            w.boxes.back().solid = false;
        }
        w.standIn(first, "race_gate", V3(g.p.x, w.height(g.p.x, g.p.z), g.p.z), g.yaw, {{"width", g.width}, {"finish", line ? 1.0 : 0.0}});
    }
    w.buildGrid();
}

// ---- The file ----------------------------------------------------------------------------
//
//   CRUMPLEZONE LEVEL 1
//   name THE WALL
//   about Twelve rock ledges up a boulder gully...
//   map 3                  mode sprint      laps 1     vehicle 2 (-1 any)
//   time 10.5              resets 1         damage 40  limit 0     medals 60 90 150
//   #section KIND DIFFICULTY TESTS KIT   (a section: see SectionInfo; comments to an older build)
//   #canvas X Z HEADINGDEG LENGTH HALF   (its cleared ground)
//   #stamp GENERATOR SEED                (if it began as a generated one)
//   #flow I J0 FX,FZ ...                 (current in the water: like water, a run of cells; a comment to older builds)
//   start X Z YAWDEG
//   gate X Z YAWDEG WIDTH  (in order; one per line)
//   rock X Z RX RY RZ TOP SEED TINT
//   box X Y Z HX HY HZ YAWDEG PITCHDEG ROLLDEG R G B SLAB
//   log AX AY AZ BX BY BZ R
//   tree X Z HEIGHT KIND TINT
//   station X Z YAWDEG NAME...
//   remove X Z
//   heights I J0 H H H ...   (a run of cells along j from (I, J0), meters)
//   surfaces I J0 SSPP SSPP ...  (hex: surface, paint)
//   water I J0 L L L ...     (-1 for dry)

namespace {

const double kDeg = M_PI / 180;

template <typename T, typename F>
void writeRuns(std::ostringstream& o, const char* tag, const std::map<int, T>& cells, int n, F fmt) {
    auto it = cells.begin();
    while (it != cells.end()) {
        const int i = it->first / n, j0 = it->first % n;
        o << tag << ' ' << i << ' ' << j0;
        int j = j0, count = 0;
        while (it != cells.end() && it->first == i * n + j && count < 64) {
            o << ' ' << fmt(it->second);
            ++it, ++j, ++count;
        }
        o << '\n';
    }
}

std::string num(double v, int prec = 2) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

}  // namespace

std::string saveLevelText(const Level& lv) {
    std::ostringstream o;
    o << "CRUMPLEZONE LEVEL 1\n";
    o << "name " << lv.name << '\n';
    if (!lv.about.empty()) o << "about " << lv.about << '\n';
    if (!lv.author.empty()) o << "author " << lv.author << '\n';
    static const char* modes[LEVEL_MODE_COUNT] = {"free", "race", "sprint"};
    o << "map " << lv.map << '\n' << "mode " << modes[std::min(std::max(lv.mode, 0), LEVEL_MODE_COUNT - 1)] << '\n';
    o << "laps " << lv.laps << '\n' << "vehicle " << lv.vehicle << '\n' << "time " << num(lv.timeOfDay) << '\n';
    o << "resets " << (lv.resets ? 1 : 0) << '\n' << "damage " << num(lv.maxDamage, 1) << '\n' << "limit " << num(lv.timeLimit, 1) << '\n';
    o << "medals " << num(lv.medals[0], 1) << ' ' << num(lv.medals[1], 1) << ' ' << num(lv.medals[2], 1) << '\n';
    if (lv.section.on) {
        const SectionInfo& c = lv.section;
        o << "#section " << sectionKindName(c.kind) << ' ' << c.difficulty << ' ' << sectionBitsText(c.tests, false) << ' ' << sectionBitsText(c.kit, true) << '\n';
        o << "#canvas " << num(c.x) << ' ' << num(c.z) << ' ' << num(c.heading / kDeg, 1) << ' ' << num(c.length, 1) << ' ' << num(c.half, 1) << '\n';
        if (c.gen >= 0) o << "#stamp " << c.gen << ' ' << c.genSeed << '\n';
    }
    if (lv.hasStart) o << "start " << num(lv.start.x) << ' ' << num(lv.start.z) << ' ' << num(lv.startYaw / kDeg, 1) << '\n';
    for (const Gate& g : lv.gates) o << "gate " << num(g.p.x) << ' ' << num(g.p.z) << ' ' << num(g.yaw / kDeg, 1) << ' ' << num(g.width, 1) << '\n';
    for (const LevelObject& b : lv.objects) {
        switch (b.kind) {
            case OBJ_ROCK:
                o << "rock " << num(b.p.x) << ' ' << num(b.p.z) << ' ' << num(b.size.x, 3) << ' ' << num(b.size.y, 3) << ' ' << num(b.size.z, 3) << ' '
                  << num(b.top, 3) << ' ' << b.seed << ' ' << num(b.col[0], 2) << '\n';
                break;
            case OBJ_BOX:
                o << "box " << num(b.p.x) << ' ' << num(b.p.y) << ' ' << num(b.p.z) << ' ' << num(b.size.x, 3) << ' ' << num(b.size.y, 3) << ' '
                  << num(b.size.z, 3) << ' ' << num(b.yaw / kDeg, 1) << ' ' << num(b.pitch / kDeg, 1) << ' ' << num(b.roll / kDeg, 1) << ' '
                  << num(b.col[0], 2) << ' ' << num(b.col[1], 2) << ' ' << num(b.col[2], 2) << ' ' << b.sub << '\n';
                break;
            case OBJ_LOG:
                o << "log " << num(b.p.x) << ' ' << num(b.p.y) << ' ' << num(b.p.z) << ' ' << num(b.p2.x) << ' ' << num(b.p2.y) << ' ' << num(b.p2.z) << ' '
                  << num(b.size.x, 3) << '\n';
                break;
            case OBJ_TREE: o << "tree " << num(b.p.x) << ' ' << num(b.p.z) << ' ' << num(b.size.y, 1) << ' ' << b.sub << ' ' << num(b.col[0], 2) << '\n'; break;
            case OBJ_STATION: o << "station " << num(b.p.x) << ' ' << num(b.p.z) << ' ' << num(b.yaw / kDeg, 1) << ' ' << b.name << '\n'; break;
            default: break;
        }
    }
    for (const V3& r : lv.removed) o << "remove " << num(r.x) << ' ' << num(r.z) << '\n';
    // (The grid is the same size on every map.)
    const int n = World(MAP_FREEWAY).gridSize();
    writeRuns(o, "heights", lv.heights, n, [](float h) { return num(h); });
    writeRuns(o, "surfaces", lv.surfaces, n, [](uint16_t s) {
        char b[8];
        std::snprintf(b, sizeof b, "%04x", s);
        return std::string(b);
    });
    writeRuns(o, "water", lv.water, n, [](float h) { return h < -1e8f ? std::string("-1") : num(h); });
    // (A current only where there's water.)
    std::map<int, std::pair<float, float>> flow;
    for (const auto& f : lv.flow) {
        const auto wt = lv.water.find(f.first);
        if (wt != lv.water.end() && wt->second > -1e8f && (f.second.first != 0 || f.second.second != 0)) flow.insert(f);
    }
    writeRuns(o, "#flow", flow, n, [](const std::pair<float, float>& f) { return num(f.first) + "," + num(f.second); });
    return o.str();
}

bool parseLevelText(const std::string& text, Level& lv, std::string* error) {
    Level out;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bool header = false;
    static int gridN = 0;
    if (!gridN) gridN = World(MAP_FREEWAY).gridSize();
    auto fail = [&](const std::string& why) {
        if (error) *error = "LINE " + std::to_string(lineNo) + ": " + why;
        return false;
    };
    while (std::getline(in, line)) {
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#') {  // (comments; except a section's own lines, which an older build skips like any other)
            if (line.rfind("#flow ", 0) == 0) {  // #flow I J0 FX,FZ FX,FZ ...
                std::istringstream fs(line);
                std::string tag, v;
                int i = 0, j = 0;
                fs >> tag >> i >> j;
                if (i < 0 || j < 0 || i >= gridN || j >= gridN) return fail("CELL OFF THE MAP");
                while (fs >> v && j < gridN) {
                    float fx = 0, fz = 0;
                    if (std::sscanf(v.c_str(), "%f,%f", &fx, &fz) == 2) out.flow[i * gridN + j] = {fx, fz};
                    j++;
                }
                continue;
            }
            if (line.rfind("#section ", 0) == 0 || line.rfind("#canvas ", 0) == 0 || line.rfind("#stamp ", 0) == 0) {
                std::istringstream cs(line);
                std::string tag, a, b, c;
                cs >> tag;
                SectionInfo& sc = out.section;
                if (tag == "#section") {
                    cs >> a >> sc.difficulty >> b >> c;
                    sc.on = true;
                    sc.kind = SK_CUSTOM;
                    for (int k = 0; k < SK_COUNT; k++)
                        if (a == kKindNames[k]) sc.kind = k;
                    sc.difficulty = std::min(std::max(sc.difficulty, 1), 5);
                    sc.tests = sectionBitsParse(b, false), sc.kit = sectionBitsParse(c, true);
                } else if (tag == "#canvas") {
                    double hd = 0;
                    cs >> sc.x >> sc.z >> hd >> sc.length >> sc.half;
                    sc.heading = hd * kDeg;
                } else {
                    cs >> sc.gen >> sc.genSeed;
                }
            }
            continue;
        }
        if (!header) {
            if (line.rfind("CRUMPLEZONE LEVEL", 0) != 0) return fail("NOT A CRUMPLE ZONE LEVEL");
            header = true;
            continue;
        }
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        auto rest = [&]() {
            std::string r;
            std::getline(ls, r);
            const size_t a = r.find_first_not_of(' ');
            return a == std::string::npos ? std::string() : r.substr(a);
        };
        if (tag == "name") out.name = rest();
        else if (tag == "about") out.about = rest();
        else if (tag == "author") out.author = rest();
        else if (tag == "map") ls >> out.map;
        else if (tag == "mode") {
            std::string m;
            ls >> m;
            out.mode = m == "race" ? LEVEL_RACE : m == "sprint" ? LEVEL_SPRINT : LEVEL_FREE;
        } else if (tag == "laps") ls >> out.laps;
        else if (tag == "vehicle") ls >> out.vehicle;
        else if (tag == "time") ls >> out.timeOfDay;
        else if (tag == "resets") {
            int r = 1;
            ls >> r;
            out.resets = r != 0;
        } else if (tag == "damage") ls >> out.maxDamage;
        else if (tag == "limit") ls >> out.timeLimit;
        else if (tag == "medals") ls >> out.medals[0] >> out.medals[1] >> out.medals[2];
        else if (tag == "start") {
            double yaw = 0;
            ls >> out.start.x >> out.start.z >> yaw;
            out.startYaw = yaw * kDeg;
            out.hasStart = true;
        } else if (tag == "gate") {
            Gate g;
            double yaw = 0;
            ls >> g.p.x >> g.p.z >> yaw >> g.width;
            g.yaw = yaw * kDeg;
            out.gates.push_back(g);
        } else if (tag == "rock") {
            LevelObject b;
            b.kind = OBJ_ROCK;
            ls >> b.p.x >> b.p.z >> b.size.x >> b.size.y >> b.size.z >> b.top >> b.seed >> b.col[0];
            out.objects.push_back(b);
        } else if (tag == "box") {
            LevelObject b;
            b.kind = OBJ_BOX;
            ls >> b.p.x >> b.p.y >> b.p.z >> b.size.x >> b.size.y >> b.size.z >> b.yaw >> b.pitch >> b.roll >> b.col[0] >> b.col[1] >> b.col[2] >> b.sub;
            b.yaw *= kDeg, b.pitch *= kDeg, b.roll *= kDeg;
            out.objects.push_back(b);
        } else if (tag == "log") {
            LevelObject b;
            b.kind = OBJ_LOG;
            ls >> b.p.x >> b.p.y >> b.p.z >> b.p2.x >> b.p2.y >> b.p2.z >> b.size.x;
            out.objects.push_back(b);
        } else if (tag == "tree") {
            LevelObject b;
            b.kind = OBJ_TREE;
            ls >> b.p.x >> b.p.z >> b.size.y >> b.sub >> b.col[0];
            out.objects.push_back(b);
        } else if (tag == "station") {
            LevelObject b;
            b.kind = OBJ_STATION;
            ls >> b.p.x >> b.p.z >> b.yaw;
            b.yaw *= kDeg;
            b.name = rest();
            out.objects.push_back(b);
        } else if (tag == "remove") {
            V3 r;
            ls >> r.x >> r.z;
            out.removed.push_back(r);
        } else if (tag == "heights" || tag == "surfaces" || tag == "water") {
            int i = 0, j = 0;
            ls >> i >> j;
            if (i < 0 || j < 0 || i >= gridN || j >= gridN) return fail("CELL OFF THE MAP");
            std::string v;
            while (ls >> v) {
                if (j >= gridN) return fail("RUN OFF THE MAP");
                const int id = i * gridN + j;
                if (tag == "heights") out.heights[id] = (float)std::atof(v.c_str());
                else if (tag == "surfaces") out.surfaces[id] = (uint16_t)std::strtoul(v.c_str(), nullptr, 16);
                else {
                    const double l = std::atof(v.c_str());
                    out.water[id] = l <= -1 + 1e-9 && v == "-1" ? -1e9f : (float)l;
                }
                j++;
            }
        } else {
            return fail("UNKNOWN: " + tag);
        }
        if (ls.fail() && !ls.eof()) return fail("BAD NUMBER IN " + tag);
    }
    if (!header) return fail("EMPTY");
    if (out.map < 0 || out.map >= MAP_COUNT) return fail("NO SUCH MAP");
    out.laps = std::max(1, out.laps);
    out.file = lv.file;
    lv = out;
    return true;
}

bool loadLevelFile(const std::string& path, Level& lv, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "CAN'T OPEN " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    lv.file = path;
    if (!parseLevelText(ss.str(), lv, error)) return false;
    lv.file = path;
    return true;
}

bool saveLevelFile(const Level& lv, const std::string& path, std::string* error) {
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        makeDir(configDir());
        makeDir(path.substr(0, slash));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "CAN'T WRITE " + path;
        return false;
    }
    f << saveLevelText(lv);
    return (bool)f;
}

std::string userLevelFolder() { return configDir() + "/levels"; }

std::vector<std::string> levelFolders() { return {userLevelFolder(), "levels"}; }

namespace {

std::vector<std::string> filesIn(const std::string& dir, const std::string& ext = ".czl") {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*" + ext).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do out.push_back(dir + "/" + fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) out.push_back(dir + "/" + n);
        }
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::vector<std::string> listLevelFiles(const std::string& dir, const std::string& ext) { return filesIn(dir, ext); }

std::vector<Level> listLevels() {
    std::vector<Level> out = builtinLevels();
    for (const std::string& dir : levelFolders())
        for (const std::string& path : filesIn(dir)) {
            Level lv;
            if (loadLevelFile(path, lv)) out.push_back(lv);
        }
    return out;
}
