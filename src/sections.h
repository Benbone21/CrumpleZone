#pragma once
#include <cstdint>

#include <functional>
#include <vector>

#include "level.h"
#include "world.h"

// Obstacle sections: seeded strips of hard ground about 140-180 m long, each a chokepoint to drive
// (a steep hill, a mud pit, a boulder river, a slickrock wall, dunes, a cliff shelf, a tree-choked
// track). See docs/terrain-generator-handoff.md. A section is stamped onto an existing World in its
// own frame: local +X (u) runs from the entry along the heading, local +Z (v) is to the left of that
// (+Z at heading 0). Heights are measured from the entry, so the same section can sit anywhere.
enum SectionType {
    SEC_STEEP_HILL,
    SEC_MUD_PIT,
    SEC_ROCKY_RIVER,
    SEC_ROCK_WALL,
    SEC_DUNES,
    SEC_CLIFF_SHELF,
    SEC_TIMBER,
    SEC_COUNT
};

struct SectionFrame {
    V3 entry;             // on the ground there (its y is what heights are measured from)
    double heading = 0;   // radians, the game's yaw: 0 = +X, pi/2 = -Z (any angle works; boxes turn with it)
};

// Where it ended up, once built.
struct SectionBuilt {
    V3 entry, exit;       // the trail's two ends, on the finished ground
    double exitHeading = 0;
};

// The ground a section takes over: from `apron` m before the entry to `apron` m past the exit
// (flat run-in and run-out), `half` m either side of the trail; beyond that the ground blends back
// into its surroundings over `blend` m. Everything standing on the cleared ground and its blend
// (trees, rocks, boxes...) is removed first, so keep neighbours' cleared ground apart.
struct SectionFootprint {
    double length;  // entry to exit along the heading
    double half;
    double apron;
    double blend;
};

// A dune field as a height function: summed directional waveforms (see sections.cpp). (u, v) are metres
// from the field's centre (u along the section); the result is the sand's height above the hollows. Any
// generator wanting dunes (another map, a biome) can call it per cell.
struct DuneParams {
    double windDeg = 28;                   // the wind's heading from +u; the main ridges run across it
    double spacing = 135;                  // crest to crest of the main ridges (m)
    double crestMax = 19, crestMin = 10;    // the tallest crest (in the middle), the smallest (at the edge)
    double sub = 0.4, subSpacing = 68;      // a second set of dunes at half the spacing: height as a fraction of the main ones', spacing (m); small swells over both
    double windwardDeg = 12.5, slipDeg = 32;  // the profile's slopes: a long gentle back, a short slip face at the angle of repose
    double halfU = 240, halfV = 80;        // the field's extent: the dunes die away at this superellipse
    double warp = 30, warpScale = 200;     // meander: domain-warp displacement and feature size (m)
    double bend = 30, bendLength = 340, bendDeg = 64;  // a second, longer wave (direction from the wind) that bends the ridges into crescents
    double swell = 2.4;                    // undulation of the hollows' floor (m)
};
double duneHeight(double u, double v, const DuneParams& p, uint32_t seed, double* aboveFloor = nullptr);  // (aboveFloor: the dunes alone, without the hollows' own swell)

const char* sectionName(SectionType t);  // "ROCK WALL"
double sectionLength(SectionType t);     // entry to exit (m)
SectionFootprint sectionFootprint(SectionType t);

// Stamps section `t` (the same seed: the same section) onto `w`. Call w.buildGrid() afterwards (the
// lookup grids don't know about what it added). Whatever scanned rock hulls are active
// (props::activeRockScans) are used for the boulders.
// With `record`, every rock, rock slab, tree and log it stands up is also added there as a LevelObject
// (in world coordinates), so the editor can keep it in a level and let it be edited by hand.
SectionBuilt buildSection(World& w, SectionType t, uint32_t seed, const SectionFrame& f, std::vector<LevelObject>* record = nullptr);

// Takes everything standing where `in(x, z)` is true off the ground (what a section does first).
void clearObjectsWhere(World& w, const std::function<bool(double, double)>& in);
