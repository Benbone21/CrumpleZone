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
// shell for this vehicle, if it has one. Returns false (t untouched) if it doesn't.
bool buildBodyShell(const Car& car, VisualTemplate& t);

// Wheel arch cut into the shell for an axle at x: center height and radius (m).
void shellArch(const VehicleSpec& vs, double& centerY, double& radius);
// Height of the shell's wheel opening at distance d (m) from the axle; -1e9 outside it.
double shellArchHeight(const VehicleSpec& vs, double d);
// Whether this vehicle is drawn with a sculpted shell.
bool hasBodyShell(const VehicleSpec& vs);
