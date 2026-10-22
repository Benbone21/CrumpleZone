#pragma once
#include "world.h"

// Spawn pads: where the game puts vehicles down. A poured concrete slab, flat and level, big enough
// for the trucks that start on it, with a ramp down to the natural ground ahead of it (the way
// you face), so you drive off it onto the track. The ground under it is flattened (heights, surface
// and paint), whatever stood on it is taken off, and the slab itself is drawn by the "spawn_pad" model
// (src/models/spawn_pad.cpp). The collider is the terrain, so it is as solid as the ground.
//
// Built from the same numbers wherever the world is built (maps, levels, the section park), so a
// host, its guests and the dedicated server all get the same pad.

// Where slot k stands on a pad of `cols` columns, in the pad's frame: u ahead of the pad's point,
// v to its left. One column: in a line behind the point. Two (a level's grid): two abreast, 7 m
// between rows, the left one a little ahead. Three or more (a map's start): the first on the point
// and the rest filling out each row from the middle.
constexpr double kPadRowGap = 7.0, kPadColGap = 5.6;
// The slab past its slots: behind the last row, ahead of the first and beside the outer columns. Room for
// the biggest truck that starts on it (Hell Boy, 6.8 by 3.0 m) to stand on the paint, which stops a metre
// in from the edge.
constexpr double kPadBack = 5.2, kPadFront = 5.5, kPadSide = 3.2;
inline void padSlotUV(int cols, int k, double& u, double& v) {
    cols = cols < 1 ? 1 : cols;
    const int row = k / cols, c = k % cols;
    if (cols == 2) {
        u = -(kPadRowGap * row + c * 2.0), v = c ? -2.8 : 2.8;
        return;
    }
    u = -kPadRowGap * row;
    v = kPadColGap * (c == 0 ? 0 : (c % 2 ? (c + 1) / 2 : -(c / 2)));
}

// Adds a pad at p facing `yaw` (the game's: 0 = +X) for `slots` vehicles in `cols` columns, and
// returns the pad (also kept in w.spawnPads). It's the ground there that decides its height. A pad
// laid where the ground is already a flat concrete paddock is recorded but leaves the ground alone.
// With `openAhead` m of lane ahead of it also cleared of trees, rocks and logs.
const SpawnPad& addSpawnPad(World& w, V3 p, double yaw, int slots = 1, int cols = 1, double openAhead = 0);
// A pad for the trucks parked along `spots` (all facing `yaw`): as small as holds them.
const SpawnPad& addParkingPad(World& w, const std::vector<V3>& spots, double yaw);
// The pads for a map's own start and parking, once the map is built. Moves w.spawn onto the pad's level.
void addMapSpawnPads(World& w);
