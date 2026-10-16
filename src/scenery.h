// Static scenery meshes, built once per map for the lit shader: trees (pines, broadleaf,
// dead snags), boulders, rock slabs, logs and bushes. Rocks are built to hug their
// colliders, so a tire on one sits where it looks like it should.
#pragma once
#include <vector>

#include "modelkit.h"
#include "render.h"
#include "world.h"

// (`skip`: what the scanned props draw instead, see propdraw.h.)
void buildTreeMesh(const World& world, std::vector<Vtx>& out, const ScanCover& skip = ScanCover());
void buildRockMesh(const World& world, std::vector<Vtx>& out, const ScanCover& skip = ScanCover());   // boulders, rock slabs, logs
void buildBushMesh(const World& world, std::vector<Vtx>& out, const ScanCover& skip = ScanCover());   // bushes and lakeside reeds
void buildPropMesh(const World& world, std::vector<Vtx>& out);   // fences, cabin, signs, tables, stumps

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
