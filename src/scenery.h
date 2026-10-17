// Static scenery meshes, built once per map for the lit shader (the trees are instanced: treedraw.h):
// boulders and rock slabs; reeds and logs. Rocks are built to hug their colliders, so a tire on one sits where it looks like it should.
#pragma once
#include <vector>

#include "meshopt.h"
#include "modelkit.h"
#include "render.h"
#include "world.h"

// (`skip`: what the scanned props draw instead, see propdraw.h.)
void buildRockMesh(const World& world, std::vector<Vtx>& out, const ScanCover& skip = ScanCover());   // boulders, rock slabs
void buildBushMesh(const World& world, std::vector<Vtx>& out, const ScanCover& skip = ScanCover());   // reeds and fallen logs (the bushes are instanced: forest::buildBushes)
void buildPropMesh(const World& world, std::vector<Vtx>& out);   // fences, cabin, signs, tables, stumps
void buildBoxMesh(const World& world, std::vector<Vtx>& out);    // the level's concrete blocks (World::boxes)
// Both go through meshopt.h as they're built (bottoms in the ground; the boxes also faces one hides in another, flat
// regions merged): `simplifyScenery() = false` leaves them as made (tools/meshaudit).
bool& simplifyScenery();
// What they're simplified with: world-sized, the ground under them; `whole`: everything meshopt does (the boxes), else just
// the bottoms in the ground (the models, each simplified already: an editor drag rebuilds them all).
MeshOptOptions sceneryOptions(const World& world, bool whole);

// A code-made model (modelkit.h) into lit-shader vertices: its origin at p, its front turned
// to `yaw`, scaled along (front, up, left).
// `tint` multiplies its colours (a tree a shade darker or lighter than the next).
void appendModel(std::vector<Vtx>& out, const mk::Mesh& m, const V3& p, double yaw, const V3& scale = V3(1, 1, 1), float tint = 1);
// ...or turned any way: its front, up and left (orthonormal) in the world, scaled along them.
void appendModel(std::vector<Vtx>& out, const mk::Mesh& m, const V3& p, const V3 axes[3], const V3& scale);
void appendModel(std::vector<Vtx>& out, const mk::Mesh& m, const V3& p, const V3 axes[3], double scale = 1);
// A placed model, whichever way it's placed.
void appendPlaced(std::vector<Vtx>& out, const Placed& pl, const mk::Mesh& m);
// The mesh for a placed model: the picked one, or (with its own params) built once per set.
const mk::Mesh& placedMesh(const Placed& pl);
// Which design a placed model shows: its own (editor, lab), else a fitted part (bare) the first in use, else one of those in use by where it stands.
int placedVariant(const Placed& pl);
