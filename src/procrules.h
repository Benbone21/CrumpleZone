// The procedural world's rules (docs/plans/procworld-next.md "Rules"): the numbers every layer of the generator reads, in one place.
// Tune the world here, not in the code that reads them. tests/procworld.cpp checks the world keeps to them.
#pragma once

namespace proc {

// ---- Roads and trails ------------------------------------------------------------------------------------------------------------
enum RoadKind { RK_PAVED, RK_GRAVEL, RK_TRAIL, RK_TWOTRACK, RK_COUNT };
struct RoadRule {
    const char* name;
    double halfWidth;  // m: the tread's half-width
    double gradeMax;   // the steepest the routing lets its grade out to before it accepts a deeper cut or fill
    double fillMax;    // m: the deepest cut or fill against the land (past it the grade is let out, up to gradeMax)
};
// paved (reserved: none yet), gravel roads, trails (green and blue; black ones are two-tracks).
inline const RoadRule kRoadRules[RK_COUNT] = {
    {"PAVED", 3.5, 0.10, 10.0},
    {"GRAVEL", 3.0, 0.35, 8.0},
    {"TRAIL", 2.0, 0.60, 2.5},
    {"TWO-TRACK", 2.0, 0.70, 1.5},
};
inline const RoadRule& roadRule(int kind) { return kRoadRules[kind < 0 || kind >= RK_COUNT ? RK_TRAIL : kind]; }
// The earthworks' side: as wide as kBatter times its cut or fill is deep (plus the tread's shoulder). Eased in (a smoothstep, whose
// steepest is 1.5 times its mean), the steepest part of a fill is 1 : kBatter / 1.5, about 1 : 1.5.
constexpr double kBatter = 2.2;

// ---- Water ----------------------------------------------------------------------------------------------------------------------
struct WaterRule {
    double freeboard;       // m: the land stands at least this over a channel's water, or the channel fades (nothing is filled)
    double riverBendMin;    // m: a river's tightest bend (its contour's radius); tighter (a ring, a hairpin) and it fades out
    double creekBendMin;    // m: a creek's
    double bankBatter;      // a valley's wall is as wide as this times its depth (eased in: steepest about 1 : bankBatter / 1.5)
    double riverValleyMax;  // m: the widest a river's valley wall goes (a gorge in the ranges is steeper: rock)
    double creekValleyMax;  // m: a creek's
};
inline constexpr WaterRule kWater = {3.0, 220.0, 110.0, 2.5, 1000.0, 160.0};

}  // namespace proc
