// A car's mechanical parts as code-made models (modelkit.h; src/models/parts.cpp and repair.cpp),
// placed on the physics nodes so they ride, flex and break with it: the crate engine on its block,
// the gearbox and transfer case behind it, a differential on each driven axle, the prop shafts and
// CV axles along their shaft beams (stretched to the beam's length), control arms, tie rods and
// radius arms along the link beams, coil-overs on the spring beams, leaf packs on the leaf-sprung
// axles. A damaged part shows it: a bent arm or tie rod is kinked, a broken shaft, arm, rod, spring or leaf pack
// hangs in two halves from its own ends with a torn, glowing break, and the lit shader tints and rims it hot
// (Placed::wear); a flooded or dead engine is sooty. On foot, `pickCarPart` finds the part under the crosshair for
// its name and condition (and the repair that goes with it). Meant for the frame's `Renderer::frameModels`; no
// OpenGL here.
#pragma once
#include <string>
#include <vector>

#include "fixes.h"
#include "softbody.h"

enum CarPartLevel {
    PARTS_BIG = 1,    // engine, gearbox, transfer case, differentials, prop shafts: what shows from a distance
    PARTS_ALL = 2,    // ...and the half shafts, links, springs
};

enum PartKind {
    PK_ENGINE, PK_GEARBOX, PK_TRANSFER, PK_DIFF, PK_PROP_SHAFT, PK_CV_AXLE,
    PK_LOWER_ARM, PK_UPPER_ARM, PK_TIE_ROD, PK_TRAILING, PK_COILOVER, PK_LEAF,
};
enum PartState { PS_OK, PS_BENT, PS_BROKEN, PS_FLOODED, PS_DEAD, PS_DAMAGED };
const char* partStateText(PartState s);  // "BROKEN"

// What a placement is: which part of the car (the halves and torn ends of a snapped part share one).
struct PartTag {
    size_t index = 0;     // its place in the Placed list it was appended to
    PartKind kind = PK_ENGINE;
    PartState state = PS_OK;
    int wheel = -1, axle = -1, beam = -1, beam2 = -1;  // the corner, axle or beam(s) it belongs to (-1: none)
    std::string name;     // "FRONT LEFT CV AXLE" (only filled in when tags are asked for)
    std::string detail;   // the engine's make, the gearbox's
    std::string label() const;  // "FRONT LEFT CV AXLE: BROKEN", "ENGINE: 350 V8 2-BARREL, FLOODED"
};

// Appends the car's parts (nothing for a prop or a car without wheels). `tags`: one entry per appended
// placement (for the hover).
void carPartModels(const Car& c, std::vector<Placed>& out, int level = PARTS_ALL, std::vector<PartTag>* tags = nullptr);

// Placements kept from frame to frame so laying out a car's parts makes no garbage: recyclePlaced() takes back a
// list of them (and empties it), takePlaced() hands one out reset (its id string and params keep their room).
void recyclePlaced(std::vector<Placed>& v);
Placed takePlaced();

// Where a placed part's U-joint is, in the world: a gearbox's, transfer case's (`front`: its front output) or differential's
// yoke, whose centre a drive shaft's joint sits on. (The placement itself for any other part.)
V3 partYoke(const Placed& pl, bool front = false);
// A placed drive shaft's two joint centres (its -X and +X). false if it isn't a drive shaft.
bool driveShaftJoints(const Placed& pl, V3& a, V3& e);

struct PartHit {
    bool ok = false;
    int car = -1;         // (set by the caller that looks through several cars)
    PartTag tag;
    double t = 0;         // m along the ray
    V3 at;                // where it hit
};
// The part of `c` a ray from `eye` along `dir` meets within `maxDist`: its placements' boxes (the model's bounds,
// padded a few cm so a thin shaft can be aimed at), nearest first. `occlude`: not through the car's own skin or
// its tires (the body hides what's under it; the parts view has none), except a bent or broken part, which glows
// through the body for you.
bool pickCarPart(const Car& c, const V3& eye, const V3& dir, double maxDist, bool occlude, PartHit& hit);
// Is `f` the repair for this part?
bool partMatchesFix(const PartTag& t, const Fix& f);
