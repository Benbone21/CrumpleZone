#pragma once
#include <cstdint>

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

const char* sectionName(SectionType t);  // "ROCK WALL"
double sectionLength(SectionType t);     // entry to exit (m)
SectionFootprint sectionFootprint(SectionType t);

// Stamps section `t` (the same seed: the same section) onto `w`. Call w.buildGrid() afterwards (the
// lookup grids don't know about what it added). Whatever scanned rock hulls are active
// (props::activeRockScans) are used for the boulders.
SectionBuilt buildSection(World& w, SectionType t, uint32_t seed, const SectionFrame& f);
