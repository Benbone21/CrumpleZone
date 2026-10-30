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
    k.push_back({W, "gully_area", nullptr, AT(r.water.gullyArea), "km2: a channel drains this much before it cuts a gully (dry)..."});
    k.push_back({W, "creek_area", nullptr, AT(r.water.creekArea), "km2: ...a creek (water, fordable)..."});
    k.push_back({W, "river_area", nullptr, AT(r.water.riverArea), "km2: ...a river..."});
    k.push_back({W, "big_river_area", nullptr, AT(r.water.bigRiverArea), "km2: ...a big river (deep and fast: crossed at its known fords, or on a bridge)"});
    k.push_back({W, "bank_batter", nullptr, AT(r.water.bankBatter), "in the ranges a valley's side is this times its height wide (the steepest, at its foot, 1 : bank_batter/2)"});
    k.push_back({W, "low_batter", nullptr, AT(r.water.lowBatter), "...in the lowland"});
    k.push_back({W, "river_grade", nullptr, AT(r.water.riverGrade), "the steepest a big river's level climbs in the ranges (a tenth of it in the lowland)"});
    k.push_back({W, "creek_grade", nullptr, AT(r.water.creekGrade), "...and a creek's, high in the ranges (cascades)"});
    k.push_back({W, "ford_spacing", nullptr, AT(r.water.fordSpacing), "m: about how far apart a big river's known fords are"});
    k.push_back({W, "ford_depth", nullptr, AT(r.water.fordDepth), "m: the water over a ford's riffle"});
    const char* BR = "bridges";
    k.push_back({BR, "damage", nullptr, AT(r.bridge.damage), "0..1: how worn a rough bridge is on the whole (planks gone, broken and sagging; rails and posts)"});
    k.push_back({BR, "fair_share", nullptr, AT(r.bridge.fairShare), "the share of bridges in fair shape (main trails' 1.4 times as often, other trails' half as often)"});
    k.push_back({BR, "missing_share", nullptr, AT(r.bridge.missingShare), "the share of the deck's plank pieces missing on the roughest bridge"});
    k.push_back({BR, "max_gap", nullptr, AT(r.bridge.maxGap), "m: the widest hole on the one drivable line every bridge keeps (keep it under ~0.9)"});
    const char* T = "terrain.ranges";
    k.push_back({T, "belt_size", nullptr, AT(r.range.beltSize), "m: about how far apart the ranges are across the world's grain (the slow belt field's features; the world's scale, 0.85-1.2, times it)"});
    k.push_back({T, "belt_stretch", nullptr, AT(r.range.beltStretch), "...and how many times longer they run along the grain (long ridgelines)"});
    k.push_back({T, "belt_from", nullptr, AT(r.range.beltFrom), "0..1: where the belt field starts raising a range (less 0.06 x the world's relief)"});
    k.push_back({T, "belt_full", nullptr, AT(r.range.beltFull), "0..1: where a range is at full height (its rough, its biome)"});
    k.push_back({T, "desert_sink", nullptr, AT(r.range.desertSink), "the belt field less this in hot dry country: the ranges give way to mesa and desert there"});
    k.push_back({T, "belt_top", nullptr, AT(r.range.beltTop), "0..1: where its valley floors and mass have risen whole (the range's axis: under it, the foothills)"});
    k.push_back({T, "pass_height", nullptr, AT(r.range.passHeight), "m over the lowland: the ranges' valley floors at their heart (times the world's relief)"});
    k.push_back({T, "peak_height", nullptr, AT(r.range.peakHeight), "m: the mountains' mass over their valley floors, times the world's relief (0.75-1.25): the valleys are cut into it"});
    k.push_back({T, "summit_height", nullptr, AT(r.range.summitHeight), "m: the summits on a range's axis over its mass, times the world's relief: the peaks (1.5-2.5 km)"});
    k.push_back({T, "summit_size", nullptr, AT(r.range.summitSize), "m: about how big the summit massifs' field is (a massif of peaks every so far along a range's axis)"});
    k.push_back({T, "summit_from", nullptr, AT(r.range.summitFrom), "0..1: where that field starts raising summits..."});
    k.push_back({T, "summit_full", nullptr, AT(r.range.summitFull), "0..1: ...and where they're whole"});
    k.push_back({T, "summit_carve", nullptr, AT(r.range.summitCarve), "0..1: the most of a summit's height its gullies cut (crests and spurs between them, not domes)"});
    k.push_back({T, "crest_spacing", nullptr, AT(r.range.crestSpacing), "m: about how far apart the crags on the high ground are (its rough detail)"});
    k.push_back({T, "alpine_from", nullptr, AT(r.range.alpineFrom), "m: the alpine biome starts to take over here..."});
    k.push_back({T, "alpine_full", nullptr, AT(r.range.alpineFull), "m: ...and is whole here"});
    k.push_back({T, "alpine_heat_shift", nullptr, AT(r.range.alpineHeatShift), "m: in the coldest country the alpine comes this much lower (in the hottest, higher)"});
    const char* MS = "terrain.mesas";
    k.push_back({MS, "height", nullptr, AT(r.mesa.height), "m: a mesa's top over the desert floor"});
    k.push_back({MS, "size", nullptr, AT(r.mesa.size), "m: about how big the mesa field's features are (mesas, buttes between)"});
    k.push_back({MS, "cliff", nullptr, AT(r.mesa.cliff), "0..1: the share of its height in the sheer band at the top (the rest the talus under it)"});
    k.push_back({MS, "spires", nullptr, AT(r.mesa.spires), "0..1: how many lone spires and needles stand on the flats"});
    const char* KN = "terrain.knolls";
    k.push_back({KN, "knoll_size", nullptr, AT(r.knoll.knollSize), "m: the rolling knolls' size (the land's lumps, after the Backcountry's)"});
    k.push_back({KN, "knoll_height", nullptr, AT(r.knoll.knollHeight), "m: ...and their height, top to hollow, about"});
    k.push_back({KN, "hill_size", nullptr, AT(r.knoll.hillSize), "m: the hills' size"});
    k.push_back({KN, "hill_height", nullptr, AT(r.knoll.hillHeight), "m: ...and their height"});
    k.push_back({KN, "knob_size", nullptr, AT(r.knoll.knobSize), "m: the rocky knobs' size (sharp-crested)"});
    k.push_back({KN, "knob_height", nullptr, AT(r.knoll.knobHeight), "m: ...and their height"});
    k.push_back({KN, "floor_clear", nullptr, AT(r.knoll.floorClear), "m over a channel's level where they're whole (lower they fade: valley floors stay at their water)"});
    k.push_back({KN, "channel_clear", nullptr, AT(r.knoll.channelClear), "m: within this of a creek's or river's bank they only rise"});
    k.push_back({KN, "desert", nullptr, AT(r.knoll.desert), "0..1: how much of them the dunes and badlands keep"});
    k.push_back({KN, "mountains", nullptr, AT(r.knoll.mountains), "0..1: ...and the alpine (it has its own crags)"});
    const char* SR = "rocks.slope";
    k.push_back({SR, "scree_from", nullptr, AT(r.slopeRock.screeFrom), "rise over run: slopes steeper than this take extra rocks (scree, outcrops)..."});
    k.push_back({SR, "scree_per_ha", nullptr, AT(r.slopeRock.screePerHa), "...this many a hectare tried at cliff_from, fewer down to none at scree_from"});
    k.push_back({SR, "scree_size", nullptr, AT(r.slopeRock.screeSize), "m: their radius at cliff_from (0.8 at scree_from), give or take a half: car-sized, to crawl over"});
    k.push_back({SR, "cliff_from", nullptr, AT(r.slopeRock.cliffFrom), "rise over run: steeper than this the face is a rock wall (big slabs laid along it and sunk in)..."});
    k.push_back({SR, "cliff_full", nullptr, AT(r.slopeRock.cliffFull), "...wholly covered from here"});
    k.push_back({SR, "tor_share", nullptr, AT(r.slopeRock.torShare), "0..1: of the wall's rocks, this share stand up out of it instead: outcrops and tors..."});
    k.push_back({SR, "tor_size", nullptr, AT(r.slopeRock.torSize), "m: ...this big (house-sized and more), give or take a third"});
    k.push_back({SR, "cliff_per_ha", nullptr, AT(r.slopeRock.cliffPerHa), "slabs tried a hectare of rock wall (packed: they overlap)"});
    k.push_back({SR, "cliff_size", nullptr, AT(r.slopeRock.cliffSize), "m: a slab's radius across the face, give or take a third (its thickness about a third of it)"});
    k.push_back({SR, "base_reach", nullptr, AT(r.slopeRock.baseReach), "m: below a rock wall, a boulder field this far out from its foot..."});
    k.push_back({SR, "base_per_ha", nullptr, AT(r.slopeRock.basePerHa), "...this many a hectare tried, the biggest nearest the wall"});
    k.push_back({SR, "desert", nullptr, AT(r.slopeRock.desert), "0..1: how much of them the badlands and dunes keep (rounder rock)"});
    k.push_back({SR, "lowland", nullptr, AT(r.slopeRock.lowland), "0..1: ...and the meadows and woods (the alpine keeps them all)"});
    const char* SC = "scatter";
    k.push_back({SC, "rock_rate", nullptr, AT(r.scatter.rockRate), "the loose rocks tried, times the old scatter's (the Backcountry's ~11 a hectare)"});
    k.push_back({SC, "rock_size", nullptr, AT(r.scatter.rockSize), "...and their size, times the old scatter's (big enough to crawl over)"});
    k.push_back({SC, "cluster_share", nullptr, AT(r.scatter.clusterShare), "0..1: of those placed, this share are a cluster's middle: bigger, with smaller ones round it..."});
    k.push_back({SC, "cluster_rocks", nullptr, AT(r.scatter.clusterRocks), "...about this many"});
    k.push_back({SC, "logs_per_ha", nullptr, AT(r.scatter.logsPerHa), "fallen logs a hectare tried in the woods (the meadows' groves half that)"});
    k.push_back({SC, "bush_thicket", nullptr, AT(r.scatter.bushThicket), "0..1: how much the bushes gather in thickets (0: evenly)"});
    k.push_back({SC, "blue_rocks", nullptr, AT(r.scatter.blueRocks), "rocks beside a blue trail's tread, every 100 m"});
    k.push_back({SC, "black_rocks", nullptr, AT(r.scatter.blackRocks), "...beside a black one's"});
    k.push_back({SC, "black_tread", nullptr, AT(r.scatter.blackTread), "...and low ones on a black one's tread (driven over), every 100 m"});
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
    const char* E = "erosion";
    k.push_back({E, "on", nullptr, AT(r.erosion.on), "1: the land is worn by water and frost (gullies, fans, silted floors, scree); 0: not"});
    k.push_back({E, "coarse_iterations", nullptr, AT(r.erosion.coarseIterations), "the erosion's steps, on the 8 m grid the land is worn on (more: deeper worn, slower)"});
    k.push_back({E, "iterations", nullptr, AT(r.erosion.iterations), "(unused: a 4 m pass after the 8 m one; it needs the land sampled four times as often)"});
    k.push_back({E, "max_cut", nullptr, AT(r.erosion.maxCut), "m: the most one step cuts anywhere"});
    k.push_back({E, "max_depth", nullptr, AT(r.erosion.maxDepth), "m: the worn land stays within this of the land before"});
    k.push_back({E, "glacier_iterations", nullptr, AT(r.erosion.glacierIterations), "the ice ages' steps, before the rivers: glaciers scoop cirques and U-shaped troughs, leave moraines (0: none)"});
    k.push_back({E, "glacier_line", nullptr, AT(r.erosion.glacierLine), "m: ice gathers above this and melts below it"});
    k.push_back({E, "glacier_cut", nullptr, AT(r.erosion.glacierCut), "how hard the ice wears its bed (m a step per square root of the ice passing, m3 a year)"});
    k.push_back({E, "dam_min", nullptr, AT(r.erosion.damMin), "m: a valley's natural dam (a rock bar, a landslide) stands at least this much over its channel..."});
    k.push_back({E, "dam_max", nullptr, AT(r.erosion.damMax), "m: ...and at most this much (each site's dice: lakes of varied sizes)"});
    k.push_back({E, "lake_share", nullptr, AT(r.erosion.lakeShare), "0..1: the share of gentle valley stretches with a natural dam (rock bar, landslide) across them: the lakes"});
    k.push_back({E, "lake_min_area", nullptr, AT(r.erosion.lakeMinArea), "ha: water the worn land holds behind a valley's natural dam is a lake from this big..."});
    k.push_back({E, "lake_min_depth", nullptr, AT(r.erosion.lakeMinDepth), "m: ...and this deep (twice as deep for a hollow no dam made)"});
    k.push_back({E, "lake_open_area", nullptr, AT(r.erosion.lakeOpenArea), "ha: a hollow no dam made (a sink of the drainage, a low spot left by the wear) holds a lake only from this big"});
    k.push_back({E, "pit_fill", nullptr, AT(r.erosion.pitFill), "0..1: how much of the load reaching a closed hollow silts it (the rest settles: lakes last)"});
    {
        static const char* const kErSec[5] = {"erosion.meadow", "erosion.forest", "erosion.badlands", "erosion.alpine", "erosion.dunes"};
        static double* (*const ec[5])(Rules&) = {AT(r.erosion.biome[0].cut), AT(r.erosion.biome[1].cut), AT(r.erosion.biome[2].cut), AT(r.erosion.biome[3].cut), AT(r.erosion.biome[4].cut)};
        static double* (*const ed[5])(Rules&) = {AT(r.erosion.biome[0].deposit), AT(r.erosion.biome[1].deposit), AT(r.erosion.biome[2].deposit), AT(r.erosion.biome[3].deposit), AT(r.erosion.biome[4].deposit)};
        static double* (*const et[5])(Rules&) = {AT(r.erosion.biome[0].talus), AT(r.erosion.biome[1].talus), AT(r.erosion.biome[2].talus), AT(r.erosion.biome[3].talus), AT(r.erosion.biome[4].talus)};
        static double* (*const er[5])(Rules&) = {AT(r.erosion.biome[0].creep), AT(r.erosion.biome[1].creep), AT(r.erosion.biome[2].creep), AT(r.erosion.biome[3].creep), AT(r.erosion.biome[4].creep)};
        for (int b = 0; b < 5; b++) {
            k.push_back({kErSec[b], "cut", nullptr, ec[b], "stream power: m a step a channel draining 1 ha cuts at slope 1 (gullies; deeper as the square root of what drains)"});
            k.push_back({kErSec[b], "deposit", nullptr, ed[b], "0..2: how readily the load drops where the slope eases (fans at slope feet, silted flat floors)"});
            k.push_back({kErSec[b], "talus", nullptr, et[b], "the steepest the ground holds (rise over run): steeper slides down to it (scree; 1.4 and up: cliffs stand)"});
            k.push_back({kErSec[b], "creep", nullptr, er[b], "0..0.2: soil creep rounding the land (soft rolling ground; near 0 keeps rock crests sharp)"});
        }
    }
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
