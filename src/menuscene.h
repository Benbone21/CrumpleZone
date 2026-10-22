// The main menu's background: a camp in a clearing at golden hour, built from the game's own pieces (the wilds terrain, the cabin, the
// campfire, the woodpile and tent models, trees, rocks, three of the listed vehicles parked) and shown by the ordinary renderer with a
// camera that drifts slowly. Plain launches build it; scripted and headless runs never do (main.cpp).
#pragma once
#include "level.h"
#include "render.h"

namespace menuscene {

constexpr double kHours = 18.4;  // the sun low and warm

// The camp as a level on the wilds (not on any list): ground, cabin, fire, trees, rocks. The fire is at fire().
Level level();
World buildWorld();       // the camp's world: the forest's own trees and rocks taken off the clearing, then level()'s on it
V3 fire();                // x, z of the fire (y 0: the ground under it is the world's)
struct Spot {
    double x, z, yaw;     // where a parked vehicle stands and which way it faces (the car convention)
};
// The three places for the vehicles (the camp's left, right and back), given which of the lineup stands at each.
const Spot* spots();
// The slow drift of the camera at time t (s): round the camp a few degrees and in and out a little.
View camera(const World& w, double t);
// The fire's light this frame: a flicker (t in s).
float flicker(double t);
// How much the menu's panels need the background dimmed on the left (0..1), for the painter.
}  // namespace menuscene
