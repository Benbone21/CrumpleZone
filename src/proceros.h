// Hydraulic and thermal erosion of a heightfield grid (the procedural world's land: procworld.cpp erodes it in fixed tiles, see
// docs/plans/procworld-next.md "Slice: real erosion").
//
// A pure function of its input grid: fixed iteration counts, fixed visiting orders (ties broken by the cell's index), only + - * /
// and sqrt (correctly rounded everywhere), no threads. So every machine makes the same bytes from the same grid (-ffp-contract=off).
//
// Each iteration:
//  1. Water: every cell gets its rain and passes it, with what it carries, to its lower neighbours (in proportion to slope^2), visited
//     from the highest cell down, so a cell's drainage area A is everything upstream of it.
//  2. Stream power with deposition (Yuan et al. 2019): a cell is cut by K sqrt(A) S (its steepest slope S), never below its lowest
//     receiver, and lets fall G Qs / A of the sediment Qs coming into it. So steep gullies cut, and where the slope eases the load
//     drops: fans at the feet of slopes, flat floors in the valleys, closed hollows silted up.
//  3. Talus: ground steeper than its angle of repose slides down to it (scree slopes, sharp crests); soil creep rounds what's left
//     (strong in the plains, almost none on the high rock).
// Cells marked fixed (a channel's water, the grid's edge) pass water and load on but are never changed.
#pragma once
#include <cstdint>
#include <vector>

namespace proc {

struct ErosionGrid {
    int n = 0;          // cells a side (n x n, index a * n + b: a along x, b along z)
    double cell = 4;    // m between cells
    std::vector<float> h;        // the heights: in, eroded in place
    std::vector<float> kErode;   // per cell: stream power (m an iteration at 1 ha drained, slope 1)
    std::vector<float> gDep;     // ...deposition (G: 0 none, 1 drops the upstream mean erosion rate)
    std::vector<float> talus;    // ...the angle of repose (rise over run)
    std::vector<float> creep;    // ...soil creep (0..0.2 of the Laplacian an iteration)
    std::vector<uint8_t> fixed;  // ...never changed (1)
    std::vector<float> floor;    // ...never worn lower than this (the drainage's valley floor: its channels stay the lowest ground)
};

struct ErosionRun {
    int coarseIterations = 0;  // first, on every other cell (twice the spacing; the grid's side must be odd): the broad forms, cheaply
    int iterations = 40;       // then on the grid itself
    int talusPasses = 2;      // talus passes an iteration
    double rain = 1;          // water a cell (the area's unit: A is in m2 times this)
    double maxCut = 1.5;      // m: the most a cell is cut in one iteration
    double talusRate = 0.4;   // 0..0.5: how much of a slope's excess over its repose slides in one pass
    // Glaciers first (the ice ages: before the rivers): ice gathers above glacierLine and flows down the land's fall lines, wearing
    // its bed by how much ice passes (not by slope: so it overdeepens, cirque floors and troughs scooped below their lips), and drops
    // its load where it melts out (moraines). Its basins are where tarns and valley lakes stand. 0 iterations: none.
    int glacierIterations = 0;
    double glacierLine = 1500;   // m: the ice gathers above this (melts below it, 1 m a year per 100 m)
    double glacierCut = 0.004;   // m an iteration per sqrt(m3 of ice a year passing)
    double glacierMax = 90;      // m: the deepest the ice wears anywhere
    double pitFill = 0.25;       // 0..1: how much of the load reaching a closed hollow silts it (the rest settles out of sight: lakes last)
};

// Erodes g.h in place. Returns the drainage area of the last iteration (m2, per cell) if `area` is given.
void erodeGrid(ErosionGrid& g, const ErosionRun& run, std::vector<float>* area = nullptr);

// The lakes of a grid: the closed hollows (fillDepressions) over minArea m2 and minDepth deep. level: the water's level per cell
// (-1e30: dry); returns how many lakes.
int findLakes(int n, double cell, const std::vector<float>& h, std::vector<float>& level, double minArea, double minDepth);

// Fills the closed hollows of a grid (priority flood, Barnes et al. 2014): `filled` is the lowest surface over h that drains every
// cell off the grid's edge (flat over a hollow: its water level, at its spill point). Deterministic (ties by index).
void fillDepressions(int n, const std::vector<float>& h, std::vector<float>& filled);

}  // namespace proc
