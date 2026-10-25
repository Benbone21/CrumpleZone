// A career world being played: the profile and the world loaded, your vehicles out where you
// left them, and the saves (every kAutosaveEvery seconds, and on every way out: quit, window
// closed, back to the main menu, another map). main.cpp only calls these; the tests drive them.
//
// The cars in the sim know which saved vehicle they are by Car::careerSlot.
#pragma once
#include <string>

#include "career.h"
#include "careerworld.h"

class Sim;

struct CareerSession {
    Career profile;
    WorldSave world;
    double sinceSave = 0;
    static constexpr double kAutosaveEvery = 180;
    bool active = false;  // begun and not ended (sandbox: never)
    std::string player;   // your name (a world's left vehicles are by owner)
};

namespace careersession {

// Into a world: loads the profile (profile.txt, its .bak, or the old career.txt), sets
// profile.on, and replaces the sim's vehicles (not its props or other players') with yours.
// startVehicle >= 0: a new world, started in that vehicle (a catalog index): the profile
// starts fresh at the world's difficulty if there was none at all, and the vehicle is added to
// it, at the map's spawn. Otherwise it's continued: every profile vehicle left in this world
// (and in a Shared crew world every vehicle left in it) is built and put back where and how it
// was left, you in the one you drove last. A wheel that was off its hub (taken off, or shed)
// is back on: the loose wheels lying about aren't saved. A world with none of yours in it
// gets you a starter at the spawn.
void begin(CareerSession& s, Sim& sim, const WorldSave& w, const std::string& playerName, int startVehicle = -1);
// Each of the session's cars' condition written back into profile.vehicles / world.left.
void capture(CareerSession& s, const Sim& sim);
// Counts the time played; true (and the profile and world saved) once kAutosaveEvery has passed since the last save.
bool tick(CareerSession& s, const Sim& sim, double dt);
// Out of the world: capture, both saved, and the session over. Nothing when it isn't active.
void end(CareerSession& s, const Sim& sim);

}  // namespace careersession
