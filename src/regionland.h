// The country grown round the big map's regions (docs/plans/regions2.md, onemap.h): each old 1.2 km map sits in the middle of
// country of its own kind, a few kilometres of it, made the way the maps' own land is (noise, no new pipeline).
//
//   The Mountain: alpine ranges round its valley, higher toward the map's south-west corner (snow from 205 m, pines to ~175 m).
//   The Frontier: badlands of tiered mesas, slickrock domes and sandy washes to the south and west; to the east a cold high bench
//                 cut by a canyon with a frozen creek in it.
//   The Open Country: rolling meadows, its northern part wooded hills, its southern part red-rock mesas and canyons.
#pragma once
#include <cstdint>

#include "onemap.h"

// A region's own land at (x, z) on the big map (its old map's middle is laid over it, World::stitchRegionLand): smooth, the world
// adds the metre-scale bumps.
double regionLand(const MapRegion& r, double x, double z);
// The big map's land at (x, z): the Backcountry's own there (`backcountry`) blended into the regions' by their weights. Exactly
// `backcountry` within kSeamFrom of the middle.
double blendRegionLand(double x, double z, double backcountry);
// How deep a wash or gully cuts the land there (0..1: its bed), for the land and the ground's paint.
double regionWash(double x, double z);
// What a region's ground is made of at a cell (paint and surface), from its height, slope and the kinds of country there. False: leave
// it as the land's natural ground (grass, the shader's rock on the steep).
bool regionGround(const MapRegion& r, double x, double z, double h, double slope, uint8_t& paint, uint8_t& surface);
