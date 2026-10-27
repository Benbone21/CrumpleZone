// The procedural world's rules (docs/plans/procworld-next.md "Rules"): every number the generator's layers read, in one place, as
// data. The compiled defaults are below; the game reads assets/procworld/rules.txt over them at startup (F5 restarts the game, so
// it rereads them), and a missing file, or a bad line in it, leaves the default. Tests use the defaults. Tune the world in the
// file, not in the code that reads it. tests/procworld.cpp checks the world keeps to them, and that the shipped file is the defaults.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace proc {

// ---- Roads and trails ------------------------------------------------------------------------------------------------------------
enum RoadKind { RK_PAVED, RK_GRAVEL, RK_TRAIL, RK_TWOTRACK, RK_COUNT };
struct RoadRule {
    double halfWidth;  // m: the tread's half-width
    double gradeMax;   // the steepest the routing lets its grade out to before it accepts a deeper cut or fill
    double fillMax;    // m: the deepest cut or fill against the land (past it the grade is let out, up to gradeMax)
};

// ---- Water ----------------------------------------------------------------------------------------------------------------------
struct WaterRule {
    // (The drainage network, procworld.cpp: a lattice node every 160 m drains to its lowest neighbour; its drainage is the area
    // upstream of it. docs/plans/procworld-next.md "Slice: land shape with real drainage".)
    double gullyArea = 0.08;         // km2: a channel drains this much before it cuts a gully (dry)...
    double creekArea = 0.3;          // km2: ...a creek (water, fordable)...
    double riverArea = 1.5;          // km2: ...a river...
    double bigRiverArea = 2.5;       // km2: ...a big river (deep and fast: crossed at its known fords, or on a bridge)
    double bankBatter = 6.0;         // in the ranges a valley's side is this times its height wide (steepest at its foot: 1 : batter/2)
    double lowBatter = 14;            // ...in the lowland
    double riverGrade = 0.04;        // the steepest a big river's level climbs in the ranges (a tenth of it in the lowland)
    double creekGrade = 0.33;        // ...and a creek's high in the ranges (cascades)
    double fordSpacing = 2500;       // m: about how far apart a big river's known fords are
    double fordDepth = 0.4;          // m: the water over a ford's riffle
};

// ---- Terrain: the mountain ranges ---------------------------------------------------------------------------------------------
struct RangeRule {
    double beltFrom = 0.40;        // the belt field (0..1, ~9 km features) where ranges begin (less 0.06 x the world's relief)
    double beltFull = 0.60;        // ...and where they're at full height
    double passHeight = 300;       // m over the lowland: the ranges' valley floors at their heart (times the world's relief, 0.75-1.25)
    double peakHeight = 620;        // m: the mountains' mass over their valley floors, times the world's relief (the valleys are cut into it)
    double summitHeight = 2900;    // m: the summits on a range's axis over its mass, times the world's relief: the peaks (1.5-2.5 km)
    double crestSpacing = 1100;    // m: about how far apart the crags on the ridges are (the rough detail on the high ground)
    double alpineFrom = 330;       // m: the alpine biome's weight starts here...
    double alpineFull = 560;       // ...and is whole here
    double alpineHeatShift = 100;    // m: in the coldest country the alpine comes this much lower (and in the hottest this much higher)
};

// ---- Alpine: forest, tree line, rock and snow -----------------------------------------------------------------------------------
struct AlpineRule {
    double treesPerHa = 420;        // trees a hectare on the slopes at the foot of the range (pines)
    double treeSlopeMax = 0.75;     // no trees steeper than this (rise over run, ~35 degrees)
    double treeLine = 1550;        // m: where the forest ends...
    double treeLineWobble = 80;    // m: ...give or take this (a noise: higher on some spurs, lower in others)
    double treeLineFade = 250;     // m: below the line the forest thins over this band...
    double krummholz = 0.6;        // ...and the trees shrink to (1 - this) of their height at the line
    double meadowSize = 110;       // m: about how big the meadows are
    double trunkSpacing = 3.0;     // m: trees stand at least this far apart (drivable between, tight in places)
    double undergrowthPerHa = 360; // bushes a hectare tried under the trees (kept only near trees, below the tree line)
    double logsPerHa = 14;         // fallen logs a hectare tried under the trees (drive over or round them)
    double stumpsPerHa = 18;       // stumps a hectare tried under the trees
    double meadowShare = 0.05;      // of the slopes are open meadows (glades a couple of hundred metres across)
    double coniferShare = 0.93;     // pines and firs; the rest dead snags...
    double aspenShare = 0.1;       // ...or aspens, low down:
    double aspenBelow = 900;       // m: aspens only below this
    double rockBandSlope = 0.5;      // slopes steeper than this show rock bands (9: none)...
    double rockBandSpacing = 24;   // m: ...ledges about this far apart up the slope
    double rockBandWidth = 0.3;    // ...this share of each step bare rock
    double screeSlope = 0.4;       // above the tree line, slopes steeper than this are scree (where a patch noise says)...
    double rockSlope = 0.8;        // ...and steeper than this bare rock
    double snowDust = 1350;         // m: a dusting of snow begins (drawn: no snow underfoot), thickening up to snowFull...
    double snowDustCover = 0.45;    // ...where it covers at most this share of the ground (the snow underfoot is the patches)
    double snowPatchy = 1600;       // m: patches of snow (snow underfoot) begin...
    double snowFull = 2000;         // m: ...and lie whole from here
    double snowNorth = 200;          // m: the snow comes this much lower on a north face
    double snowHollow = 150;         // m: ...and this much lower in a hollow (a gully 10 m or more below the ground 32 m round)
    double snowSlopeMax = 0.85;    // steeper than this the snow slides off: rock
    double rocksPerHa = 45;        // loose rocks and boulders tried a hectare (steep ground keeps more of them)
};

// ---- Trees by biome --------------------------------------------------------------------------------------------------------------
struct TreeRule {
    double perHa;       // trees a hectare at most
    double groveMin;    // away from groves, this share of it
    double conifer;     // shares of the trees: conifers, snags, broadleaf (the rest)
    double snag;
};

// ---- Erosion: the land worn by water and frost (proceros.h; procworld.cpp erodes it in fixed tiles) ---------------------------------
struct ErosionBiomeRule {
    double cut;      // stream power: m an iteration a channel draining 1 ha cuts at slope 1 (gullies; more drained, deeper: as its square root)
    double deposit;  // 0..2: how readily the load drops where the slope eases (fans at slope feet, flat silted floors)
    double talus;    // the steepest ground holds (rise over run): steeper slides down to it (scree; 1.4: cliffs stand)
    double creep;    // 0..0.2: soil creep, rounding (soft rolling land; ~0 keeps rock crests sharp)
};
struct ErosionRule {
    double on = 1;            // 1: the land is eroded; 0: not (the land shaped round its drainage alone)
    double coarseIterations = 50;  // the erosion's steps on an 8 m grid first (the broad gullies and spurs)...
    double iterations = 12;   // ...then on the 4 m grid (the fine ones)
    double maxCut = 4;        // m: the most a step cuts anywhere
    double maxDepth = 60;     // m: the eroded land stays within this of the land it wore
    double glacierIterations = 0;   // (experimental, off: its flow lines are the whole grid's, so tiles disagree at seams) the ice ages' steps (before the rivers): glaciers above glacier_line scoop cirques and troughs
    double glacierLine = 1500;      // m: ice gathers above this, melts below
    double glacierCut = 0.004;      // how hard the ice wears its bed (m a step per square root of the ice passing, m3 a year)
    double pitFill = 0.25;          // 0..1: how much of the load reaching a closed hollow silts it (lakes last)
    // meadow, forest, badlands (desert and mesa), mountains (alpine), dunes
    ErosionBiomeRule biome[5] = {{0.5, 2.4, 0.6, 0.1}, {0.6, 2.4, 0.7, 0.06}, {2.8, 3.6, 1.4, 0.0}, {2.0, 1.8, 0.9, 0.01}, {0.16, 3.0, 0.6, 0.05}};
};

struct Rules {
    RoadRule road[RK_COUNT] = {
        {3.5, 0.10, 10.0},  // paved (reserved: none yet)
        {3.0, 0.45, 2.5},   // main trails (were gravel roads: the owner's world has trails only)
        {2.0, 0.60, 2.5},   // trails (green and blue)
        {2.0, 0.70, 1.5},   // two-tracks (black)
    };
    double batter = 2.5;      // earthworks' sides (and a place's levelled lot) are this times their depth wide (eased in: the steepest part about 1 : 1.5)
    WaterRule water;
    RangeRule range;
    AlpineRule alpine;
    // meadow, forest, badlands, mountains (its density and shares: alpine), dunes
    ErosionRule erosion;
    TreeRule trees[5] = {{22, 0.15, 0.2, 0.04}, {98, 0.55, 0.45, 0.06}, {6, 1.0, 0.0, 0.55}, {27, 1.0, 0.9, 0.1}, {0.7, 1.0, 0.0, 0.55}};
};

// The rules in force (the defaults until loadRules).
const Rules& rules();
inline const RoadRule& roadRule(int kind) { return rules().road[kind < 0 || kind >= RK_COUNT ? RK_TRAIL : kind]; }

// Reads a rules file over the defaults (keys it doesn't name keep theirs). Returns false (and the rules stay as they were) if it
// can't be read; a bad line is skipped and noted in `warnings`. Call before any land is made (the game: at startup).
bool loadRules(const std::string& path, std::vector<std::string>* warnings = nullptr);
// The same, from a file's text into `r` (keys it doesn't name keep r's values). (The tests: never the rules in force.)
bool parseRules(const std::string& text, Rules& r, std::vector<std::string>* warnings = nullptr, const std::string& name = "rules");
void resetRules();  // back to the compiled defaults
// The game and the server at startup: assets/procworld/rules.txt (or $CZ_PROC_RULES), its bad lines noted on stderr.
void loadShippedRules();
// The rules as a file (every key, its value and what it does): what assets/procworld/rules.txt is made from.
std::string rulesText(const Rules& r);
// A hash of the rules in force: 0 for the defaults. (Multiplayer: two machines with other rules make other land.)
uint32_t rulesStamp();

}  // namespace proc
