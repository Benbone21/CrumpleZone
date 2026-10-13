// Static scenery meshes, built once per map for the lit shader: trees (pines, broadleaf,
// dead snags), boulders, rock slabs, logs and bushes. Rocks are built to hug their
// colliders, so a tire on one sits where it looks like it should.
#pragma once
#include <vector>

#include "render.h"
#include "world.h"

void buildTreeMesh(const World& world, std::vector<Vtx>& out);
void buildRockMesh(const World& world, std::vector<Vtx>& out);   // boulders, rock slabs, logs
void buildBushMesh(const World& world, std::vector<Vtx>& out);   // bushes and lakeside reeds
void buildPropMesh(const World& world, std::vector<Vtx>& out);   // fences, cabin, signs, tables, stumps
