// A vehicle as a picture for the menus: its real body (the game's own visual template, at rest), its bolt-on parts, running gear
// and tyres, made into one model for Renderer::icon() (which draws it three-quarters from the front, lit, in a small texture).
#pragma once
#include "modelkit.h"

struct VehiclePreview {
    mk::Mesh mesh;
    double massKg = 0;   // the whole vehicle, as built
};

// The preview of vehicleCatalog()[catalogIndex], or null while its visual template is still being built on a worker (it's
// started on the first ask; ask again next frame). Built once and kept.
const VehiclePreview* vehiclePreview(int catalogIndex);
