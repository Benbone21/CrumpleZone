// A career world being played: the profile and the world loaded, your vehicles out where you
// left them, and the saves (every kAutosaveEvery seconds, and on every way out: quit, window
// closed, back to the main menu, another map). main.cpp only calls these; the tests drive them.
//
// The cars in the sim know which saved vehicle they are by Car::careerSlot.
#pragma once
#include <map>
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
    bool write = true;    // false: nothing is ever written (scripted runs; the saves still count as done)
    std::map<int, int> placed;  // careerSlot -> the catalog vehicle put out for it (a car since swapped for another isn't saved over it)
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
// Each of the session's cars' condition written back into profile.vehicles / world.left (not a car that is no longer
// the vehicle put out for its slot: swapped for another type, it would turn the owned one into that).
void capture(CareerSession& s, const Sim& sim);
// Counts the time played; true (and the profile and world saved) once kAutosaveEvery has passed since the last save.
bool tick(CareerSession& s, const Sim& sim, double dt);
// Out of the world: capture, both saved, and the session over. Nothing when it isn't active.
void end(CareerSession& s, const Sim& sim);

// Changing vehicles in a career world: only where it can be done (a repair station's pad; later phases add outposts and
// camps), and only into one of your own (profile) vehicles, as it was saved, never with parts you don't have.
bool canSwapHere(const Sim& sim);
// The profile vehicle of that catalog vehicle to change into (not the one you're in; one in this world first); -1 none.
int ownedOf(const CareerSession& s, const Sim& sim, int vehicle);
// You into profile vehicle k: the one you're in stays parked where it is (its condition captured), and k comes out
// beside it as it was saved (from wherever it stood in this world; from another world or none it's delivered here).
// False (and why) away from a station, for a vehicle you don't own, or the one you're in.
bool swapTo(CareerSession& s, Sim& sim, int k, std::string& why);

// A new world (not saved) whose slug is free of the saved worlds and of any the profile's vehicles still name.
WorldSave newWorldFor(const Career& profile, const std::string& name, int map, Difficulty d, WorldMode m);
// Deletes a world (deleteWorld). The profile's vehicles in it are kept, in no world (not put out anywhere); the profile
// is saved if any were. With write false nothing is touched on disk. False if the folder couldn't be removed.
bool forgetWorld(Career& profile, const std::string& slug, bool write);

}  // namespace careersession
