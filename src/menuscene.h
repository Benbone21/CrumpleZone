// The main menu's background: a camp in a clearing at sunset, built from the game's own pieces (the wilds terrain, the cabin, the campfire, the
// woodpile and tent models, trees, rocks, a river, three of the listed vehicles parked) and shown by the ordinary renderer with a camera that
// drifts slowly. The camp is data: a level (menucamp.h) loaded from the player's own file, else the game's, else made by code. Plain launches build
// it; scripted and headless runs never do (main.cpp).
#pragma once
#include <string>

#include "daylight.h"
#include "level.h"
#include "render.h"

namespace menuscene {

constexpr double kHours = 17.55;       // where the sun stands when the level doesn't say (about 12 degrees up, behind the camp: its disc clears the ridge)
constexpr double kSunsetLook = 18.55;  // the sky and cloud colours are the ones of this hour (sunset)

std::string userPath();   // ~/.config/crumplezone/menu_camp.czl: what the editor saves (never the game's file)
std::string repoPath();   // assets/menu/camp.czl: the layout the game ships
// The camp: the player's file, else the game's, else generateDefault(). `from` says which.
Level load(std::string* from = nullptr);
Level generateDefault();  // the shipped layout, made by code (tools: --menu-camp-dump FILE writes it)
bool dump(const std::string& path);
bool resetUser();         // forgets the player's file (the game's layout is back)
World buildWorld(const Level& camp);
// The camera at time t (s) and the camp's sky.
View camera(const Level& camp, const World& w, double t);
Daylight daylight(const Level& camp);
// The fire's light this frame: a flicker (t in s).
float flicker(double t);

}  // namespace menuscene
