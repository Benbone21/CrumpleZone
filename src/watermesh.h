#pragma once
#include "render.h"

// The water surface as a triangle soup: a fan per lake (maps without a water grid), or the sheet of 2 m quads over the wet
// cells of the height grid (rivers, carrying the current in the colour) with flat runs of the same current cut again into
// fewer triangles (meshopt.h). The water shader works per pixel from the position, so it needs no tessellation of its own.
void buildWaterMesh(const World& world, std::vector<Vtx>& out);
