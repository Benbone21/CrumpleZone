#include "sectionfile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "platform.h"

namespace {

const double kDeg = M_PI / 180;

// The proving ground's open ground in the north-west (found by looking: 320 x 150 m round it holds no
// road, course, yard, park section or water, and the pad is 175 m out from the spawn).
constexpr double kCanvasX = -310, kCanvasZ = 465, kCanvasLength = 260, kCanvasHalf = 45;

// Bilinear sample of a grid of nu x nv at (u, v) in the frame (clamped to the edge).
double sampleGrid(const SectionFile& s, const std::vector<float>& g, double u, double v) {
    const double gi = clampd(u - s.u0, 0, std::max(0, s.nu - 1)), gj = clampd(v + s.half, 0, std::max(0, s.nv - 1));
    const int i = std::min((int)gi, std::max(0, s.nu - 2)), j = std::min((int)gj, std::max(0, s.nv - 2));
    const int i1 = std::min(i + 1, s.nu - 1), j1 = std::min(j + 1, s.nv - 1);
    const double fi = gi - i, fj = gj - j;
    return (g[s.at(i, j)] * (1 - fj) + g[s.at(i, j1)] * fj) * (1 - fi) + (g[s.at(i1, j)] * (1 - fj) + g[s.at(i1, j1)] * fj) * fi;
}

int nearestCell(const SectionFile& s, double u, double v) {
    const int i = std::min(std::max((int)std::lround(u - s.u0), 0), s.nu - 1), j = std::min(std::max((int)std::lround(v + s.half), 0), s.nv - 1);
    return s.at(i, j);
}

std::string num(double v, int prec = 2) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

}  // namespace

V3 sectionToLocal(const SectionFrame& f, const V3& p) {
    const double dx = p.x - f.entry.x, dz = p.z - f.entry.z, c = std::cos(f.heading), s = std::sin(f.heading);
    return V3(dx * c - dz * s, p.y - f.entry.y, dx * s + dz * c);
}

V3 sectionToWorld(const SectionFrame& f, const V3& l) {
    const double c = std::cos(f.heading), s = std::sin(f.heading);
    return V3(f.entry.x + l.x * c + l.z * s, f.entry.y + l.y, f.entry.z - l.x * s + l.z * c);
}

SectionInfo defaultSectionCanvas() {
    SectionInfo c;
    c.x = kCanvasX, c.z = kCanvasZ, c.heading = 0, c.length = kCanvasLength, c.half = kCanvasHalf;
    return c;
}

// ---- Level -> section --------------------------------------------------------------------

bool extractSection(const World& w, const Level& lv, SectionFile& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (!lv.section.on) return fail("NOT A SECTION");
    if (!lv.hasStart) return fail("NO ENTRY: SET THE START (COURSE TOOL)");
    if (lv.gates.empty()) return fail("NO EXIT: ADD A GATE AT THE FAR END (COURSE TOOL)");
    const SectionInfo& c = lv.section;
    SectionFrame f;
    f.entry = V3(lv.start.x, w.height(lv.start.x, lv.start.z), lv.start.z);
    f.heading = c.heading;
    // Where the entry sits in the canvas (u from the canvas's back edge).
    const double cu = (lv.start.x - c.x) * std::cos(c.heading) - (lv.start.z - c.z) * std::sin(c.heading);
    if (cu < 0 || cu > c.length || c.outside(lv.start.x, lv.start.z) > 0) return fail("THE ENTRY IS OUTSIDE THE CANVAS");
    SectionFile s;
    s.name = lv.name, s.about = lv.about, s.author = lv.author;
    s.info = c;
    s.info.x = s.info.z = s.info.heading = 0;
    s.u0 = -cu, s.u1 = c.length - cu, s.half = c.half;
    s.nu = (int)std::floor(s.u1 - s.u0 + 1e-6) + 1, s.nv = (int)std::floor(2 * s.half + 1e-6) + 1;
    s.h.assign((size_t)s.nu * s.nv, 0);
    s.surf.assign((size_t)s.nu * s.nv, 0);
    s.water.assign((size_t)s.nu * s.nv, -1e9f);
    s.flowU.assign((size_t)s.nu * s.nv, 0), s.flowV.assign((size_t)s.nu * s.nv, 0);
    const double ch = std::cos(f.heading), sh = std::sin(f.heading);
    for (int i = 0; i < s.nu; i++)
        for (int j = 0; j < s.nv; j++) {
            const V3 p = sectionToWorld(f, V3(s.u0 + i, 0, -s.half + j));
            const int k = s.at(i, j), id = w.cellIndex(p.x, p.z);
            s.h[k] = (float)(w.height(p.x, p.z) - f.entry.y);
            if (id >= 0) s.surf[k] = (uint16_t)(w.cellSurface(id) << 8 | w.cellPaint(id));
            const double wl = w.waterLevel(p.x, p.z);
            if (wl > -1e8) {
                s.water[k] = (float)(wl - f.entry.y);
                const V3 fl = w.waterFlow(p.x, p.z);
                s.flowU[k] = (float)(fl.x * ch - fl.z * sh), s.flowV[k] = (float)(fl.x * sh + fl.z * ch);
            }
        }
    for (const LevelObject& o : lv.objects) {
        const V3 at = o.kind == OBJ_LOG ? (o.p + o.p2) * 0.5 : o.p;
        if (c.outside(at.x, at.z) > 0) {
            s.dropped++;
            continue;
        }
        LevelObject q = o;
        q.p = sectionToLocal(f, o.p);
        if (o.kind == OBJ_LOG) q.p2 = sectionToLocal(f, o.p2);
        if (o.kind == OBJ_ROCK || o.kind == OBJ_TREE || o.kind == OBJ_STATION) q.p.y = 0;  // (they stand on the ground)
        q.yaw = wrapAngle(o.yaw - f.heading);
        s.objects.push_back(q);
    }
    for (const Gate& g : lv.gates) {
        Gate q = g;
        q.p = sectionToLocal(f, V3(g.p.x, f.entry.y, g.p.z));
        q.p.y = 0;
        q.yaw = wrapAngle(g.yaw - f.heading);
        s.gates.push_back(q);
    }
    out = s;
    return true;
}

// ---- Section -> world --------------------------------------------------------------------

SectionBuilt placeSection(World& w, const SectionFile& s, const SectionFrame& f) {
    auto outsideOf = [&](double u, double v) { return std::max({s.u0 - u, u - s.u1, std::fabs(v) - s.half}); };
    // Everything standing on the ground and its blend goes first.
    clearObjectsWhere(w, [&](double x, double z) {
        const V3 l = sectionToLocal(f, V3(x, f.entry.y, z));
        return outsideOf(l.x, l.z) < s.blend + 1;
    });
    // The ground: every cell the canvas and its blend touch.
    if (s.nu > 0 && s.nv > 0) {
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (double uu : {s.u0 - s.blend, s.u1 + s.blend})
            for (double vv : {-s.half - s.blend, s.half + s.blend}) {
                const V3 p = sectionToWorld(f, V3(uu, 0, vv));
                x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
            }
        const bool wet = w.hasWater();
        for (int xi = (int)std::floor(x0); xi <= (int)std::ceil(x1); xi++)
            for (int zi = (int)std::floor(z0); zi <= (int)std::ceil(z1); zi++) {
                const int id = w.cellIndex(xi, zi);
                if (id < 0) continue;
                const V3 c = w.cellCenter(id);
                const V3 l = sectionToLocal(f, V3(c.x, f.entry.y, c.z));
                const double d = outsideOf(l.x, l.z);
                if (d >= s.blend) continue;
                const double wgt = d <= 0 ? 1.0 : smoothstep(s.blend, 0, d);
                const double orig = w.cellHeight(id), target = f.entry.y + sampleGrid(s, s.h, l.x, l.z);
                w.setCellHeight(id, (float)(orig + (target - orig) * wgt));
                if (wgt <= 0.5) continue;
                const int k = nearestCell(s, l.x, l.z);
                w.setCellSurface(id, (uint8_t)(s.surf[k] >> 8), (uint8_t)(s.surf[k] & 0xff));
                if (s.water[k] > -1e8f) {
                    w.setCellWater(id, (float)(f.entry.y + s.water[k]));
                    const double fu = s.flowU[k], fv = s.flowV[k], ch = std::cos(f.heading), sh = std::sin(f.heading);
                    w.flowX[id] = (float)(fu * ch + fv * sh), w.flowZ[id] = (float)(-fu * sh + fv * ch);
                } else if (wet) {
                    w.setCellWater(id, -1e9f);
                    w.flowX[id] = w.flowZ[id] = 0;
                }
            }
    }
    // Then what stands on it.
    for (const LevelObject& o : s.objects) {
        LevelObject q = o;
        q.p = sectionToWorld(f, o.p);
        if (o.kind == OBJ_LOG) q.p2 = sectionToWorld(f, o.p2);
        q.yaw = o.yaw + f.heading;
        placeObject(q, w);
    }
    SectionBuilt out;
    out.entry = f.entry;
    out.entry.y = w.height(out.entry.x, out.entry.z);
    const V3 e = sectionToWorld(f, V3(s.exitLocal().x, 0, s.exitLocal().z));
    out.exit = V3(e.x, w.height(e.x, e.z), e.z);
    out.exitHeading = f.heading + (s.gates.empty() ? 0 : s.gates.back().yaw);
    return out;
}

// ---- The file ----------------------------------------------------------------------------
//
//   CRUMPLEZONE SECTION 1
//   name / about / author / #section / #stamp   as in a level
//   #canvas U0 -HALF 0 LENGTH HALF              (the canvas in the frame: from the entry)
//   gate U V YAWDEG WIDTH                       (in order; the last is the exit)
//   rock U V RX RY RZ TOP SEED TINT             objects as in a level, in the frame:
//   box U DY V HX HY HZ YAWDEG PITCHDEG ROLLDEG R G B SLAB     (U along, V to the left, DY up from the entry)
//   log AU ADY AV BU BDY BV R
//   tree U V HEIGHT KIND TINT
//   station U V YAWDEG NAME...
//   blend 24
//   hgt I J0 H H H ...      a run of samples along v from (I, J0): heights above the entry
//   srf I J0 SSPP ...       surfaces (hex: surface, paint)
//   wtr I J0 L L ...       water above the entry (x: dry); rows with none are left out
//   flw I J0 U,V U,V ...   the current in it (m/s along, left); rows with none are left out
//
// (So a section is a level's own text with its cells in the frame: the same lines read the same.)

std::string saveSectionText(const SectionFile& s) {
    Level tmp;
    tmp.name = s.name, tmp.about = s.about, tmp.author = s.author;
    tmp.map = MAP_PROVING_GROUND, tmp.mode = LEVEL_SPRINT;
    tmp.objects = s.objects, tmp.gates = s.gates;
    tmp.section = s.info;
    tmp.section.on = true;
    tmp.section.x = s.u0, tmp.section.z = -s.half, tmp.section.heading = 0, tmp.section.length = s.u1 - s.u0, tmp.section.half = s.half;
    std::string t = saveLevelText(tmp);
    t.replace(0, t.find('\n'), "CRUMPLEZONE SECTION 1");
    std::ostringstream o;
    o << t << "blend " << num(s.blend, 1) << '\n';
    for (int i = 0; i < s.nu; i++)
        for (int j0 = 0; j0 < s.nv; j0 += 64) {
            const int j1 = std::min(s.nv, j0 + 64);
            o << "hgt " << i << ' ' << j0;
            for (int j = j0; j < j1; j++) o << ' ' << num(s.h[s.at(i, j)]);
            o << "\nsrf " << i << ' ' << j0;
            for (int j = j0; j < j1; j++) {
                char b[8];
                std::snprintf(b, sizeof b, "%04x", s.surf[s.at(i, j)]);
                o << ' ' << b;
            }
            o << '\n';
            bool any = false;
            for (int j = j0; j < j1; j++) any |= s.water[s.at(i, j)] > -1e8f;
            if (!any) continue;
            o << "wtr " << i << ' ' << j0;
            for (int j = j0; j < j1; j++) o << ' ' << (s.water[s.at(i, j)] > -1e8f ? num(s.water[s.at(i, j)]) : std::string("x"));
            o << '\n';
            bool flows = false;
            for (int j = j0; j < j1; j++) flows |= s.flowU[s.at(i, j)] != 0 || s.flowV[s.at(i, j)] != 0;
            if (!flows) continue;
            o << "flw " << i << ' ' << j0;
            for (int j = j0; j < j1; j++) o << ' ' << num(s.flowU[s.at(i, j)]) << ',' << num(s.flowV[s.at(i, j)]);
            o << '\n';
        }
    return o.str();
}

bool parseSectionText(const std::string& text, SectionFile& out, std::string* error) {
    std::istringstream in(text);
    std::string line, levelText = "CRUMPLEZONE LEVEL 1\n";
    struct Row {
        char kind;
        std::string rest;
    };
    std::vector<Row> rows;
    double blend = 24;
    bool header = false;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        if (error) *error = "LINE " + std::to_string(lineNo) + ": " + why;
        return false;
    };
    while (std::getline(in, line)) {
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!header) {
            if (line.rfind("CRUMPLEZONE SECTION", 0) != 0) return fail("NOT A CRUMPLE ZONE SECTION");
            header = true;
            continue;
        }
        if (line.rfind("hgt ", 0) == 0) rows.push_back({'h', line.substr(4)});
        else if (line.rfind("srf ", 0) == 0) rows.push_back({'s', line.substr(4)});
        else if (line.rfind("wtr ", 0) == 0) rows.push_back({'w', line.substr(4)});
        else if (line.rfind("flw ", 0) == 0) rows.push_back({'f', line.substr(4)});
        else if (line.rfind("blend ", 0) == 0) blend = std::atof(line.c_str() + 6);
        else levelText += line + '\n';
    }
    if (!header) return fail("EMPTY");
    Level lv;
    std::string err;
    if (!parseLevelText(levelText, lv, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!lv.section.on) return fail("NO #section LINE");
    SectionFile s;
    s.name = lv.name, s.about = lv.about, s.author = lv.author;
    s.info = lv.section;
    s.u0 = lv.section.x, s.half = lv.section.half, s.u1 = s.u0 + lv.section.length, s.blend = blend;
    s.info.x = s.info.z = s.info.heading = 0;
    s.nu = (int)std::floor(s.u1 - s.u0 + 1e-6) + 1, s.nv = (int)std::floor(2 * s.half + 1e-6) + 1;
    if (s.nu < 1 || s.nv < 1 || (double)s.nu * s.nv > 4e6) return fail("BAD EXTENT");
    s.h.assign((size_t)s.nu * s.nv, 0);
    s.surf.assign((size_t)s.nu * s.nv, 0);
    s.water.assign((size_t)s.nu * s.nv, -1e9f);
    s.flowU.assign((size_t)s.nu * s.nv, 0), s.flowV.assign((size_t)s.nu * s.nv, 0);
    s.objects = lv.objects, s.gates = lv.gates;
    for (const Row& r : rows) {
        std::istringstream rs(r.rest);
        int i = 0, j = 0;
        rs >> i >> j;
        if (i < 0 || j < 0 || i >= s.nu || j >= s.nv) return fail("CELL OFF THE SECTION");
        std::string v;
        while (rs >> v) {
            if (j >= s.nv) return fail("RUN OFF THE SECTION");
            const size_t k = s.at(i, j);
            if (r.kind == 'h') s.h[k] = (float)std::atof(v.c_str());
            else if (r.kind == 's') s.surf[k] = (uint16_t)std::strtoul(v.c_str(), nullptr, 16);
            else if (r.kind == 'f') std::sscanf(v.c_str(), "%f,%f", &s.flowU[k], &s.flowV[k]);
            else s.water[k] = v == "x" ? -1e9f : (float)std::atof(v.c_str());
            j++;
        }
    }
    out = s;
    return true;
}

bool loadSectionFile(const std::string& path, SectionFile& s, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "CAN'T OPEN " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parseSectionText(ss.str(), s, error);
}

bool saveSectionFile(const SectionFile& s, const std::string& path, std::string* error) {
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
    f << saveSectionText(s);
    return (bool)f;
}

std::string sectionPathFor(const std::string& levelPath) {
    const size_t dot = levelPath.find_last_of('.'), slash = levelPath.find_last_of("/\\");
    const std::string stem = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? levelPath.substr(0, dot) : levelPath;
    return stem + ".czs";
}

std::vector<SectionFile> listSections() {
    std::vector<SectionFile> out;
    for (const std::string& dir : levelFolders())
        for (const std::string& path : listLevelFiles(dir, ".czs")) {
            SectionFile s;
            if (loadSectionFile(path, s)) out.push_back(s);
        }
    return out;
}
