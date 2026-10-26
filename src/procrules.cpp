// The procedural world's rules as data: the key table (every rule's name, where it lives and what it does), the file reader and
// writer. See procrules.h.
#include "procrules.h"

#include "platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace proc {

namespace {
Rules gRules;

struct Key {
    const char* section;
    const char* name;
    double Rules::*top;              // a value straight in Rules, or...
    double* (*at)(Rules&);           // ...one inside it
    const char* what;                // what it does, and its units
};
#define AT(expr) [](Rules& r) -> double* { return &(expr); }
const char* const kRoadSec[RK_COUNT] = {"roads.paved", "roads.main", "roads.trail", "roads.twotrack"};
std::vector<Key> keys() {
    std::vector<Key> k;
    for (int i = 0; i < RK_COUNT; i++) {
        static double* (*const hw[RK_COUNT])(Rules&) = {AT(r.road[0].halfWidth), AT(r.road[1].halfWidth), AT(r.road[2].halfWidth), AT(r.road[3].halfWidth)};
        static double* (*const gm[RK_COUNT])(Rules&) = {AT(r.road[0].gradeMax), AT(r.road[1].gradeMax), AT(r.road[2].gradeMax), AT(r.road[3].gradeMax)};
        static double* (*const fm[RK_COUNT])(Rules&) = {AT(r.road[0].fillMax), AT(r.road[1].fillMax), AT(r.road[2].fillMax), AT(r.road[3].fillMax)};
        k.push_back({kRoadSec[i], "half_width", nullptr, hw[i], "m: the tread's half-width"});
        k.push_back({kRoadSec[i], "grade_max", nullptr, gm[i], "the steepest grade (rise over run) the routing lets it out to before it takes a deeper cut or fill"});
        k.push_back({kRoadSec[i], "fill_max", nullptr, fm[i], "m: the deepest cut or fill against the land; deeper, and the grade is let out instead"});
    }
    k.push_back({"roads", "batter", &Rules::batter, nullptr, "earthworks' sides (and places' levelled lots) are this times their depth wide (the steepest part about 1 : batter/1.5)"});
    const char* W = "water";
    k.push_back({W, "freeboard", nullptr, AT(r.water.freeboard), "m: the land stands at least this over a river's or creek's water, or the channel fades (nothing is filled)"});
    k.push_back({W, "river_bend_min", nullptr, AT(r.water.riverBendMin), "m: a river's tightest bend; tighter (a ring, a hairpin) and it fades out"});
    k.push_back({W, "creek_bend_min", nullptr, AT(r.water.creekBendMin), "m: a creek's tightest bend"});
    k.push_back({W, "bank_batter", nullptr, AT(r.water.bankBatter), "a valley's wall is this times its depth wide (the steepest part about 1 : bank_batter/1.5)"});
    k.push_back({W, "river_valley_max", nullptr, AT(r.water.riverValleyMax), "m: the widest a river valley's wall goes (gorges in the ranges are steeper)"});
    k.push_back({W, "river_cut_max", nullptr, AT(r.water.riverCutMax), "m: where the land stands this far over a river's level no valley is cut: rivers run between the mountains"});
    k.push_back({W, "creek_valley_max", nullptr, AT(r.water.creekValleyMax), "m: the widest a creek valley's wall goes"});
    const char* T = "terrain.ranges";
    k.push_back({T, "belt_from", nullptr, AT(r.range.beltFrom), "0..1: where the slow belt field (~9 km features) starts raising a range (less 0.06 x the world's relief)"});
    k.push_back({T, "belt_full", nullptr, AT(r.range.beltFull), "0..1: where a range is at full height"});
    k.push_back({T, "pass_height", nullptr, AT(r.range.passHeight), "m over the lowland: the ranges' high valleys and passes"});
    k.push_back({T, "peak_height", nullptr, AT(r.range.peakHeight), "m: the ridges' top over the passes, times the world's relief (0.75-1.25)"});
    k.push_back({T, "crest_spacing", nullptr, AT(r.range.crestSpacing), "m: about how far apart the crests are (wider: broader, gentler mountains)"});
    k.push_back({T, "alpine_from", nullptr, AT(r.range.alpineFrom), "m: the alpine biome starts to take over here..."});
    k.push_back({T, "alpine_full", nullptr, AT(r.range.alpineFull), "m: ...and is whole here"});
    k.push_back({T, "alpine_heat_shift", nullptr, AT(r.range.alpineHeatShift), "m: in the coldest country the alpine comes this much lower (in the hottest, higher)"});
    const char* A = "biome.alpine";
    k.push_back({A, "trees_per_ha", nullptr, AT(r.alpine.treesPerHa), "trees a hectare on the slopes below the tree line (pines)"});
    k.push_back({A, "tree_slope_max", nullptr, AT(r.alpine.treeSlopeMax), "no trees on ground steeper than this (rise over run; 0.7 is 35 degrees)"});
    k.push_back({A, "tree_line", nullptr, AT(r.alpine.treeLine), "m: the height the forest ends at..."});
    k.push_back({A, "tree_line_wobble", nullptr, AT(r.alpine.treeLineWobble), "m: ...give or take this (a slow noise)"});
    k.push_back({A, "tree_line_fade", nullptr, AT(r.alpine.treeLineFade), "m: below the line the forest thins out over this band"});
    k.push_back({A, "krummholz", nullptr, AT(r.alpine.krummholz), "0..1: trees at the line are (1 - this) of their height (stunted)"});
    k.push_back({A, "meadow_share", nullptr, AT(r.alpine.meadowShare), "0..1: this share of the forested slopes is open meadow"});
    k.push_back({A, "meadow_size", nullptr, AT(r.alpine.meadowSize), "m: about how big the meadows are"});
    k.push_back({A, "trunk_spacing", nullptr, AT(r.alpine.trunkSpacing), "m: trees stand at least this far apart (drivable between, tight in places)"});
    k.push_back({A, "undergrowth_per_ha", nullptr, AT(r.alpine.undergrowthPerHa), "bushes a hectare tried under the trees (kept only within 6 m of a tree, below the tree line)"});
    k.push_back({A, "logs_per_ha", nullptr, AT(r.alpine.logsPerHa), "fallen logs a hectare tried under the trees (solid: drive over or round)"});
    k.push_back({A, "stumps_per_ha", nullptr, AT(r.alpine.stumpsPerHa), "stumps a hectare tried under the trees (solid)"});
    k.push_back({A, "conifer_share", nullptr, AT(r.alpine.coniferShare), "0..1: pines and firs; the rest are dead snags, or aspens low down"});
    k.push_back({A, "aspen_share", nullptr, AT(r.alpine.aspenShare), "0..1: aspens (broadleaf) among the trees at the foot, none from aspen_below up"});
    k.push_back({A, "aspen_below", nullptr, AT(r.alpine.aspenBelow), "m: aspens only below this"});
    k.push_back({A, "rock_band_slope", nullptr, AT(r.alpine.rockBandSlope), "slopes steeper than this show bands of bare rock (ledges)"});
    k.push_back({A, "rock_band_spacing", nullptr, AT(r.alpine.rockBandSpacing), "m: about this far apart up the slope"});
    k.push_back({A, "rock_band_width", nullptr, AT(r.alpine.rockBandWidth), "0..1: this share of each step is bare rock"});
    k.push_back({A, "scree_slope", nullptr, AT(r.alpine.screeSlope), "above the tree line, slopes steeper than this are scree (in patches)"});
    k.push_back({A, "rock_slope", nullptr, AT(r.alpine.rockSlope), "slopes steeper than this are bare rock"});
    k.push_back({A, "snow_dust", nullptr, AT(r.alpine.snowDust), "m: a dusting of snow starts here (drawn only: nothing underfoot)"});
    k.push_back({A, "snow_dust_cover", nullptr, AT(r.alpine.snowDustCover), "0..1: the dusting covers at most this share of the ground (thickening from snow_dust to snow_full)"});
    k.push_back({A, "snow_patchy", nullptr, AT(r.alpine.snowPatchy), "m: patches of snow (underfoot) start here, more the higher..."});
    k.push_back({A, "snow_full", nullptr, AT(r.alpine.snowFull), "m: ...and the snow lies whole from here"});
    k.push_back({A, "snow_north", nullptr, AT(r.alpine.snowNorth), "m: the snow comes this much lower on a north face"});
    k.push_back({A, "snow_hollow", nullptr, AT(r.alpine.snowHollow), "m: ...and this much lower in a hollow (a gully 10 m or more below the ground 32 m round)"});
    k.push_back({A, "snow_slope_max", nullptr, AT(r.alpine.snowSlopeMax), "steeper than this the snow slides off (bare rock)"});
    k.push_back({A, "rocks_per_ha", nullptr, AT(r.alpine.rocksPerHa), "loose rocks and boulders tried a hectare (steep ground keeps more of them)"});
    static const char* const kBiome[5] = {"trees.meadow", "trees.forest", "trees.badlands", "trees.mountains", "trees.dunes"};
    static double* (*const ph[5])(Rules&) = {AT(r.trees[0].perHa), AT(r.trees[1].perHa), AT(r.trees[2].perHa), AT(r.trees[3].perHa), AT(r.trees[4].perHa)};
    static double* (*const gm[5])(Rules&) = {AT(r.trees[0].groveMin), AT(r.trees[1].groveMin), AT(r.trees[2].groveMin), AT(r.trees[3].groveMin), AT(r.trees[4].groveMin)};
    static double* (*const cf[5])(Rules&) = {AT(r.trees[0].conifer), AT(r.trees[1].conifer), AT(r.trees[2].conifer), AT(r.trees[3].conifer), AT(r.trees[4].conifer)};
    static double* (*const sn[5])(Rules&) = {AT(r.trees[0].snag), AT(r.trees[1].snag), AT(r.trees[2].snag), AT(r.trees[3].snag), AT(r.trees[4].snag)};
    for (int b = 0; b < 5; b++) {
        if (b == 3) continue;  // (the mountains' trees: biome.alpine)
        k.push_back({kBiome[b], "per_ha", nullptr, ph[b], "trees a hectare at most"});
        k.push_back({kBiome[b], "grove_min", nullptr, gm[b], "0..1: away from groves, this share of per_ha"});
        k.push_back({kBiome[b], "conifer", nullptr, cf[b], "0..1: the share of conifers (pines)"});
        k.push_back({kBiome[b], "snag", nullptr, sn[b], "0..1: the share of dead snags; the rest are broadleaf"});
    }
    return k;
}
#undef AT
double* where(Rules& r, const Key& k) { return k.top ? &(r.*k.top) : k.at(r); }

std::string fmt(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}
}  // namespace

const Rules& rules() { return gRules; }
void resetRules() { gRules = Rules(); }

std::string rulesText(const Rules& r0) {
    Rules r = r0;
    std::string out =
        "# Crumple Zone: the procedural world's rules (src/procrules.h). The game reads this file at startup (F5 restarts it).\n"
        "# Each line is   key = value   # what it does (units). A key left out, or a line it can't read, keeps the built-in value.\n"
        "# Changing these changes the land of every seed: players in a multiplayer game need the same file.\n";
    std::string sec;
    for (const Key& k : keys()) {
        if (sec != k.section) sec = k.section, out += "\n[" + sec + "]\n";
        std::string line = std::string(k.name) + " = " + fmt(*where(r, k));
        if (line.size() < 32) line.resize(32, ' ');
        out += line + "  # " + k.what + "\n";
    }
    return out;
}

bool parseRules(const std::string& text, Rules& out, std::vector<std::string>* warnings, const std::string& name) {
    std::istringstream f(text);
    Rules r = out;
    const std::vector<Key> ks = keys();
    std::string line, sec;
    int n = 0;
    auto trim = [](const std::string& t) {
        const size_t a = t.find_first_not_of(" \t\r"), b = t.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    };
    while (std::getline(f, line)) {
        n++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            sec = trim(line.substr(1, line.size() - 2));
            continue;
        }
        auto warn = [&](const std::string& w) {
            if (warnings) warnings->push_back(name + ":" + std::to_string(n) + ": " + w);
        };
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn("not 'key = value'");
            continue;
        }
        const std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        char* end = nullptr;
        const double v = std::strtod(val.c_str(), &end);
        if (val.empty() || !end || *end || v != v) {
            warn("'" + val + "' isn't a number");
            continue;
        }
        bool found = false;
        for (const Key& k : ks)
            if (sec == k.section && key == k.name) *where(r, k) = v, found = true;
        if (!found) warn("no rule [" + sec + "] " + key);
    }
    out = r;
    return true;
}

bool loadRules(const std::string& path, std::vector<std::string>* warnings) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return parseRules(ss.str(), gRules, warnings, path);
}

void loadShippedRules() {
    const char* env = std::getenv("CZ_PROC_RULES");  // (another file, to try rules without touching the shipped one)
    std::string path = env && *env ? env : "";
    if (path.empty()) {
        const std::string dir = findAssetDir("assets/procworld");
        if (dir.empty()) return;
        path = dir + "/rules.txt";
    }
    std::vector<std::string> warnings;
    if (!loadRules(path, &warnings)) {
        std::fprintf(stderr, "procedural world rules: can't read %s (the built-in ones)\n", path.c_str());
        return;
    }
    for (const std::string& w : warnings) std::fprintf(stderr, "procedural world rules: %s (skipped)\n", w.c_str());
    std::printf("procedural world rules: %s%s\n", path.c_str(), rulesStamp() ? " (changed from the built-in ones)" : "");
}

uint32_t rulesStamp() {
    const std::string now = rulesText(gRules), def = rulesText(Rules());
    if (now == def) return 0;
    uint32_t h = 2166136261u;
    for (unsigned char c : now) h = (h ^ c) * 16777619u;
    return h ? h : 1;
}

}  // namespace proc
