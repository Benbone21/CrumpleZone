#include "vegetation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#include "forest.h"
#include "plantgen.h"
#include "plants.h"

namespace veg {
namespace {
const char* const kZones[Z_COUNT] = {"rock", "sand", "lowland", "upland", "riparian", "wash"};

struct TreeModel {
    const char* name;
    int kind, design;
    double crown;  // radius over height
};
const TreeModel kTrees[] = {
    {"pine", 0, -1, 0.25},
    {"broadleaf", 1, -1, 0.32},
    {"snag", 2, -1, 0.2},
    {"grey_snag", 2, 0, 0.2},
    {"juniper", 3, forest::DRY_JUNIPER, 0.55},
    {"pinyon", 3, forest::DRY_PINYON, 0.4},
    {"cottonwood", 3, forest::DRY_COTTONWOOD, 0.6},
    {"tamarisk", 3, forest::DRY_TAMARISK, 0.45},
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// A plant kind's name as a palette writes it: lower case, underscores.
std::string shrubName(int kind) { return plantKindName(kind); }
bool fail(std::string* err, const std::string& why) {
    if (err) *err = why;
    return false;
}
template <class T>
const T* pick(const std::vector<T>& v, double u, double e) {
    double sum = 0;
    for (const T& s : v)
        if (e >= s.above && e <= s.below) sum += s.weight;
    if (sum <= 0) return nullptr;
    double at = std::min(std::max(u, 0.0), 0.999999) * sum;
    const T* last = nullptr;
    for (const T& s : v) {
        if (e < s.above || e > s.below) continue;
        if (at < s.weight) return &s;
        at -= s.weight, last = &s;
    }
    return last;
}
// A species line's trailing options: `crown R`, `size S`, `above E`, `below E`.
template <class T>
bool options(const std::vector<std::string>& a, size_t from, T& s, double* crown, double* size, std::string* err, const std::string& at) {
    for (size_t i = from; i < a.size(); i += 2) {
        const std::string k = lower(a[i]);
        if (i + 1 >= a.size()) return fail(err, at + k + " needs a value");
        const double v = std::atof(a[i + 1].c_str());
        if (k == "above") s.above = v;
        else if (k == "below") s.below = v;
        else if (k == "crown" && crown) *crown = std::min(1.5, std::max(0.05, v));
        else if (k == "size" && size) *size = std::min(2.0, std::max(0.3, v));
        else return fail(err, at + "unknown option " + k);
    }
    return true;
}
double smooth(double a, double b, double x) {
    if (b <= a) return x >= b ? 1 : 0;
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}
}  // namespace

const char* zoneName(int zone) { return zone >= 0 && zone < Z_COUNT ? kZones[zone] : "?"; }

const TreeSpec* ZoneRule::tree(double u, double elevation) const { return pick(trees, u, elevation); }
const ShrubSpec* ZoneRule::shrub(double u, double elevation) const { return pick(shrubs, u, elevation); }

double Palette::maxTrees() const {
    double m = 0;
    for (const ZoneRule& z : zone) m = std::max(m, z.trees.empty() ? 0.0 : z.treesPerHa);
    return m;
}
double Palette::maxShrubs() const {
    double m = 0;
    for (const ZoneRule& z : zone) m = std::max(m, z.shrubs.empty() ? 0.0 : z.shrubsPerHa);
    return m;
}

int zoneOf(const Palette& p, const Site& s, double u, double* density) {
    double d = 1;
    int z;
    if (s.cover == 1 || s.rock) z = Z_ROCK;
    else {
        const bool bareSand = s.cover == 2 || (s.cover == 0 && s.sand);
        if (s.water < p.riparianReach && s.waterRise < p.riparianRise) z = Z_RIPARIAN, d = bareSand ? p.sandWater : 1;
        else if (s.wash < p.washReach && s.washRise < p.washRise) z = Z_WASH, d = bareSand ? p.sandWater : 1;
        else if (bareSand) z = Z_SAND;
        else z = u < smooth(p.uplandFrom, p.uplandFull, s.elevation) ? Z_UPLAND : Z_LOWLAND;
    }
    if (density) *density = d;
    return z;
}

std::vector<std::string> treeSpecies() {
    std::vector<std::string> v;
    for (const TreeModel& t : kTrees) v.push_back(t.name);
    return v;
}
std::vector<std::string> shrubSpecies() {
    std::vector<std::string> v;
    for (int k = 0; k < BUSH_KIND_COUNT; k++) v.push_back(shrubName(k));
    return v;
}

bool parse(const std::string& text, Palette& out, std::string* err) {
    out = Palette();
    std::istringstream in(text);
    std::string line;
    int ln = 0, zone = -1;
    while (std::getline(in, line)) {
        ln++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        key = lower(key);
        std::vector<std::string> a;
        for (std::string w; ls >> w;) a.push_back(w);
        auto num = [&](size_t i) { return i < a.size() ? std::atof(a[i].c_str()) : 0.0; };
        const std::string at = "vegetation line " + std::to_string(ln) + ": ";
        auto need = [&](size_t n) { return a.size() >= n ? true : fail(err, at + key + " needs " + std::to_string(n) + " values"); };
        if (key == "name") {
            for (size_t i = 0; i < a.size(); i++) out.name += (i ? " " : "") + a[i];
        } else if (key == "upland") {
            if (!need(2)) return false;
            out.uplandFrom = num(0), out.uplandFull = std::max(num(0), num(1));
        } else if (key == "riparian") {
            if (!need(2)) return false;
            out.riparianReach = std::max(0.0, num(0)), out.riparianRise = num(1);
        } else if (key == "wash") {
            if (!need(2)) return false;
            out.washReach = std::max(0.0, num(0)), out.washRise = num(1);
        } else if (key == "slope") {
            if (!need(2)) return false;
            out.treeSlope = num(0), out.shrubSlope = num(1);
        } else if (key == "open") {
            if (!need(1)) return false;
            out.open = std::min(1.0, std::max(0.0, num(0)));
        } else if (key == "sand_water") {
            if (!need(1)) return false;
            out.sandWater = std::min(1.0, std::max(0.0, num(0)));
        } else if (key == "crown") {
            if (!need(1)) return false;
            out.crown = std::min(5, std::max(0, (int)num(0)));
        } else if (key == "plants") {
            if (!need(1)) return false;
            if (lower(a[0]) != "procedural" && lower(a[0]) != "classic") return fail(err, at + "plants is procedural or classic");
            out.procedural = lower(a[0]) == "procedural";
        } else if (key == "zone") {
            if (!need(3)) return false;
            zone = -1;
            for (int z = 0; z < Z_COUNT; z++)
                if (lower(a[0]) == kZones[z]) zone = z;
            if (zone < 0) return fail(err, at + "no zone called " + a[0]);
            out.zone[zone].treesPerHa = std::max(0.0, num(1)), out.zone[zone].shrubsPerHa = std::max(0.0, num(2));
        } else if (key == "tree") {
            if (zone < 0) return fail(err, at + "a tree before any zone");
            if (!need(4)) return false;
            TreeSpec t;
            t.species = lower(a[0]);
            const TreeModel* m = nullptr;
            for (const TreeModel& k : kTrees)
                if (t.species == k.name) m = &k;
            // (`plants procedural`: a generated species of that name first, plantgen.h; a classic model's name still works)
            const plantgen::Species* gs = out.procedural ? plantgen::find(t.species) : nullptr;
            if (gs && !gs->tree) gs = nullptr;
            if (gs) {
                t.kind = forest::kGenKind, t.design = gs->id;
                t.crown = gs->v("width") > 0 ? gs->v("width") : m ? m->crown : 0.3;
            } else {
                if (!m) return fail(err, at + "no tree called " + a[0]);
                t.kind = m->kind, t.design = m->design, t.crown = m->crown;
            }
            if (!options(a, 4, t, &t.crown, nullptr, err, at)) return false;
            t.weight = std::max(0.0, num(1)), t.hMin = std::max(0.5, num(2)), t.hMax = std::max(t.hMin, num(3));
            out.zone[zone].trees.push_back(t);
        } else if (key == "shrub") {
            if (zone < 0) return fail(err, at + "a shrub before any zone");
            if (!need(2)) return false;
            ShrubSpec s;
            s.species = lower(a[0]), s.kind = -1;
            for (int k = 0; k < BUSH_KIND_COUNT; k++)
                if (s.species == shrubName(k)) s.kind = k;
            const plantgen::Species* gs = out.procedural ? plantgen::find(s.species) : nullptr;
            if (gs && gs->tree) gs = nullptr;
            if (gs) s.kind = plantgen::kShrubKindBase + gs->id, s.rMin = gs->v("size", 0), s.rMax = std::max(s.rMin, gs->v("size", 1));
            else if (s.kind < 0) return fail(err, at + "no shrub called " + a[0]);
            else s.rMin = plantInfo(s.kind).rMin, s.rMax = plantInfo(s.kind).rMax;
            s.weight = std::max(0.0, num(1));
            if (!options(a, 2, s, nullptr, &s.size, err, at)) return false;
            out.zone[zone].shrubs.push_back(s);
        } else return fail(err, at + "unknown key " + key);
    }
    return true;
}

std::string findFile(const std::string& name, const std::string& dir) {
    if (name.empty()) return "";
    const std::string d = dir.empty() ? "." : dir;
    for (const std::string& p : {d + "/" + name + ".txt", d + "/../vegetation/" + name + ".txt", d + "/vegetation/" + name + ".txt"})
        if (std::ifstream(p)) return p;
    return "";
}

}  // namespace veg
