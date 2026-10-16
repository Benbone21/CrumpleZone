// A car's mechanical parts as code-made models (modelkit.h; src/models/parts.cpp and repair.cpp),
// placed on the physics nodes so they ride, flex and break with it: the crate engine on its block,
// the gearbox and transfer case behind it, a differential on each driven axle, the prop shafts and
// CV axles along their shaft beams (stretched to the beam's length), control arms, tie rods and
// radius arms along the link beams, coil-overs on the spring beams, leaf packs on the leaf-sprung
// axles. A broken beam's part isn't drawn, so the damage shows. Meant for the frame's
// `Renderer::frameModels`; no OpenGL here.
#pragma once
#include <vector>

#include "softbody.h"

enum CarPartLevel {
    PARTS_BIG = 1,    // engine, gearbox, transfer case, differentials, prop shafts: what shows from a distance
    PARTS_ALL = 2,    // ...and the half shafts, links, springs
};

// Appends the car's parts (nothing for a prop or a car without wheels).
void carPartModels(const Car& c, std::vector<Placed>& out, int level = PARTS_ALL);
