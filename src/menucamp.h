// The main menu's camp as data (menuscene.h builds the world from it): the camp is an ordinary level on the wilds (objects, river bed, cleared
// ground), and what a level has no place for rides in "#menu" comment lines of the same file (Level::menuLines): where the three parked vehicles
// stand and which they are, the camera and how much it drifts, the sky's colours. The world editor edits the level (DEV > EDIT MENU SCENE adds
// the MENU tool for these lines). No GL or SDL in here: the tests round-trip it.
#pragma once
#include <string>

#include "level.h"

namespace menucamp {

struct Vehicle {
    double x = 0, z = 0, yawDeg = 0;  // world metres; the car convention (0 faces +x, 90 faces -z)
    int slot = 0;                     // an index into listedVehicles()
};
struct Camera {
    double ex = 0, ey = 2, ez = 0;    // the eye: world x, z and metres above the ground
    double tx = 0, ty = 2, tz = 0;    // what it looks at
    double drift = 1;                 // how far it wanders: 0 holds still, 1 the default drift, 2 twice as much
};
struct Meta {
    Vehicle veh[3];
    int vehicles = 0;                 // how many of them the level has (a vehicle deleted in the editor is not parked)
    Camera cam;
    double look = 18.55;              // the sky's colours are the ones of this hour (sunset); the level's own time of day is where the sun stands
};

bool isCamp(const Level& lv);         // a level made for the menu (it has the "#menu scene" line)
Meta metaOf(const Level& lv);         // its settings (the defaults for any that are missing)
void setMeta(Level& lv, const Meta& m);  // rewrites the "#menu" lines (keeps "scene"), and moves/turns/changes the vehicles the level has (it adds none)
void upgrade(Level& lv);  // an older camp file (vehicles in "#menu veh" lines) gets its stand-in objects
int addVehicle(Level& lv, const Vehicle& v);  // a new stand-in (up to three): its number, or -1
bool sameMeta(const Meta& a, const Meta& b);
const char* const kVehicleModel = "menu_vehicle";
// Where the fire is: the first campfire in the level (false: none).
bool fireOf(const Level& lv, V3& at);

}  // namespace menucamp
