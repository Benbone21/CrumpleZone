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

// Into a world: loads the profile (profile.txt, its .bak, or the old career.txt; a scripted run's session, write off,
// reads nothing and keeps the profile it has), sets profile.on, and replaces the sim's vehicles (not its props or other
// players') with yours. A profile.txt or .bak that is there but can't be read refuses the world: false (and why), nothing
// changed or written. Only a brand-new profile (none on disk; write off: one made by startFresh) starts at the world's
// difficulty; a loaded one keeps its progress.
// startVehicle >= 0: a new world, started in that vehicle (a catalog index), added to the profile, at the map's spawn.
// Otherwise it's continued: every profile vehicle left in this world (and in a Shared crew world every vehicle left in
// it) is built and put back where and how it was left, you in the one you drove last. A wheel that was off its hub
// (taken off, or shed) is back on: the loose wheels lying about aren't saved. A world with none of yours in it: your
// most recently driven vehicle is moved here (as it was) to the spawn; a starter only when you own no vehicles at all.
bool begin(CareerSession& s, Sim& sim, const WorldSave& w, const std::string& playerName, int startVehicle = -1, std::string* why = nullptr);
// Each of the session's cars' condition written back into profile.vehicles / world.left (not a car that is no longer
// the vehicle put out for its slot: swapped for another type, it would turn the owned one into that).
void capture(CareerSession& s, const Sim& sim);
// Counts the time played; true (and the profile and world saved) once kAutosaveEvery has passed since the last save.
bool tick(CareerSession& s, const Sim& sim, double dt);
// Out of the world: capture, both saved, the session over and career mode off (profile.on: there's no career without a
// world). Nothing when it isn't active.
void end(CareerSession& s, const Sim& sim);

// Changing vehicles in a career world: only where it can be done (a repair station's pad; later phases add outposts and
// camps), and only into one of your own (profile) vehicles, as it was saved, never with parts you don't have.
bool canSwapHere(const Sim& sim);
// The profile vehicle of that catalog vehicle to change into (not the one you're in; only one in this world: until
// delivery (career phase 7) a vehicle in another world, or none, can't be had here); -1 none.
int ownedOf(const CareerSession& s, const Sim& sim, int vehicle);
// You into profile vehicle k: the one you're in stays parked where it is (its condition captured), and k comes out
// beside it as it was saved (from wherever it stood in this world). False (and why) away from a station, for a vehicle
// you don't own or that is in another world, or the one you're in.
bool swapTo(CareerSession& s, Sim& sim, int k, std::string& why);

// Supply caches: in a world, each world has its own opened ones (world.opened); career without a world, the profile's.
bool cacheOpened(const CareerSession& s, const CacheSpot& c);
// Opens it (once): its part or cash goes in the profile. In a world the profile and world are saved (write on). Returns
// what to announce ("" if opened already).
std::string openCache(CareerSession& s, const Sim& sim, const CacheSpot& c);

// The garage on your car in a world (one of the session's: careerSlot != -1): `fitted` set to the parts it has (its own,
// not the garage file's, so opening the garage doesn't rebuild it); false (fitted untouched) for any other car.
bool shopMods(const CareerSession& s, const Sim& sim, ModSet& fitted);
// Parts fitted to that car in the garage: into its profile vehicle (saved, write on). False if it isn't a session car of
// that catalog vehicle (the garage file's then).
bool fit(CareerSession& s, const Sim& sim, int vehicle, const ModSet& mods);

// A new world (not saved) whose slug is free of the saved worlds and of any the profile's vehicles still name.
WorldSave newWorldFor(const Career& profile, const std::string& name, int map, Difficulty d, WorldMode m);
// Deletes a world (deleteWorld). The profile's vehicles in it are kept, in no world (not put out anywhere); the profile
// is saved if any were. With write false nothing is touched on disk. False if the folder couldn't be removed.
bool forgetWorld(Career& profile, const std::string& slug, bool write);

}  // namespace careersession
