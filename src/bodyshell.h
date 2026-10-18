// Sculpted body shells: a hand-shaped outer skin for a vehicle, drawn instead of the
// smoothed physics cage.
//
// The shell is built from the real vehicle's lines (cross-sections along its length:
// rocker, door sides, tumblehome, roof and hood, wheel arches cut around the tires).
// Every shell vertex is tied to the physics nodes around it (trilinear weights in its
// lattice cell, or bilinear on a bolt-on panel's own sheet), so it dents, crumples and
// swings open with the physics exactly like the cage it replaces. Windows, lamps,
// grille and seams are drawn per pixel by the body shader from each vertex's rest
// position in meters, so their edges stay sharp.
#pragma once
#include "carvisual.h"

// Fills t's body (topological vertices, faces, render vertices) with the sculpted
// shell for this vehicle, if it has one. Returns false (t untouched) if it doesn't. `merge` cuts the flat runs of
// quads that move together into fewest triangles (off: the full grid, for the tests to compare against).
bool buildBodyShell(const Car& car, VisualTemplate& t, bool merge = true);

// Wheel arch cut into the shell for an axle at x: center height and radius (m).
void shellArch(const VehicleSpec& vs, double& centerY, double& radius);
// Height of the shell's wheel opening at distance d (m) from the axle; -1e9 outside it.
double shellArchHeight(const VehicleSpec& vs, double d);
// Whether this vehicle is drawn with a sculpted shell.
bool hasBodyShell(const VehicleSpec& vs);
// Half width of the drawn roof's flat top, inside its rounded edges (W/2 without a shell).
double shellRoofHalfWidth(const VehicleSpec& vs);
// Height of the drawn body's top (hood, roof) at x along it, on the center line.
double shellTopY(const VehicleSpec& vs, double x);

// A pickup's bed at its open end (rest frame, m): the end of the walls, the floor, the rail's top
// and the half-width between the walls. False for anything without a bed.
struct BedMouth {
    double x, floorY, railY, inner;
};
bool bedMouth(const VehicleSpec& vs, BedMouth& m);
