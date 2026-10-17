// The small plants the maps grow (World::Bush::kind): shrubs and scrub, ferns, reeds, cacti, flowers,
// mushrooms and mossy stones. All are scenery only (nothing to hit), and each kind is a design of the
// code-made "bush" model (src/models/plants.cpp: design number == kind). Which kinds grow where is
// decided here, so every map's scatter and the tests agree.
#pragma once
#include <cstdint>

enum PlantKind {
    BUSH_LEAFY,       // round leafy shrub (the original bush; also the editor's BUSH)
    BUSH_SAGE,        // grey-green sagebrush
    BUSH_JUNIPER,     // dark upright low conifer
    BUSH_FLOWERING,   // leafy shrub in bloom
    BUSH_FERN,        // fern clump
    BUSH_REEDS,       // reeds and cattails
    BUSH_CACTUS,      // saguaro or barrel cactus
    BUSH_YUCCA,       // spiky rosette on a short trunk
    BUSH_HEATHER,     // low alpine scrub in flower
    BUSH_TUMBLE,      // dry twiggy ball
    BUSH_MUSHROOMS,   // toadstools in moss
    BUSH_MOSSROCKS,   // small stones under moss
    BUSH_WILDFLOWERS, // a patch of flowers
    BUSH_KIND_COUNT
};

struct PlantInfo {
    const char* name;             // the editor's card
    double rMin, rMax, rDefault;  // half-width (m): what the maps scatter, what the editor places
    double cy, ry, rx;            // the pick ellipsoid, as multiples of the radius: its middle height, half-height, half-width
};
const PlantInfo& plantInfo(int kind);  // (out of range: the leafy shrub)
inline int plantKindOf(int kind) { return kind >= 0 && kind < BUSH_KIND_COUNT ? kind : BUSH_LEAFY; }

// What kind of country a plant grows in (the same names as the wilds: world.h WildsKind, in that order).
enum Habitat { HAB_MEADOW, HAB_DESERT, HAB_FOREST, HAB_CANYON, HAB_WETLAND, HAB_UPLAND, HAB_COUNT };
// The kind to grow at a spot of `habitat`; `u` in [0, 1) picks from the mix (use plantRoll for a position), `edge`: at the edge of a wood.
int plantKindFor(int habitat, bool edge, double u);
bool plantGrowsIn(int habitat, int kind);
// A radius for a plant of `kind`; `u` in [0, 1).
double plantRadius(int kind, double u);
// A repeatable number in [0, 1) for a spot, with a salt: scatters pick kinds from this, not from the map's own random numbers,
// so adding kinds moves nothing else.
double plantRoll(double x, double z, int salt = 0);
