// A body-on-frame truck's frame as code-made models (modelkit.h; src/models/frames.cpp), strung along
// the physics nodes of its ladder frame (softbody.h `Car::ladder`) so it bends and crumples with the
// soft body: a rail section between every two stations of each rail (boxed or C-channel, by the
// truck), the crossmembers (the dropped gearbox one, tubes or channels), spring hangers and
// shackle mounts, shock towers and control-arm pivots with the stays that reach out to them, body
// mounts, recovery points, the transfer case's skid plate and the four jack pads. Meant for the
// frame's `Renderer::frameModels`, next to carPartModels (carparts.h); no OpenGL here.
#pragma once
#include <vector>

#include "carparts.h"

// Where a jack's saddle meets the frame's jack pad: out past the rail's outside face, and the pad's depth
// under the rail's underside (frames.cpp's kPadIn and kPadT). Car::jackPoint is this point.
constexpr double kJackPadOut = 0.04;
constexpr double kJackPadDepth = 0.02;

// Appends the frame's models (nothing for a unibody, a prop or a car without wheels). `level` as for
// carPartModels: the rails, crossmembers, recovery points and skid plate show from a distance; the
// brackets, mounts and pads only up close.
void carFrameModels(const Car& c, std::vector<Placed>& out, int level = PARTS_ALL);
