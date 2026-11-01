// Car mods: engine, suspension, transmission, wheels and tires for each vehicle.
//
// Every mod is a change to the vehicle's spec (torque curve, ride height, spring
// rates, tires, gear ratios, diffs...), and the car is rebuilt from the modified spec,
// so a lift kit really raises the body on the springs and bigger tires really change
// the gearing. Option 0 of every slot is the factory part. Each slot has the factory part and
// two or three upgrades with plain names ("BIG V8", "LIFT KIT", "MUD TIRES"). What the lists
// were before parts list 2 and where each old part went: docs/parts-simplified.md.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "paint.h"
#include "vehicles.h"

enum ModSlot { MOD_ENGINE, MOD_SUSPENSION, MOD_TRANSMISSION, MOD_WHEELS, MOD_TIRES, MOD_SLOTS };
const char* modSlotName(int slot);

struct ModOption {
    std::string name;    // "BIG V8"
    std::string detail;  // what it changes, in a few words
    std::function<void(VehicleSpec&)> apply;
    // Where the part comes from: catalog vehicle and the part's id among its own (a tread: vehicle
    // -1 and the TireType). The id is the part's number in its key, not its place in the list.
    int fromVehicle = -1, fromOption = 0;
    int tier = 1;  // how a career has it: 1 everyday (shops), 2 serious kit (shops, dear), 3 the best (caches and gold medals)
};

struct ModSet {
    int pick[MOD_SLOTS] = {0, 0, 0, 0, 0};
    bool stock() const {
        for (int p : pick)
            if (p) return false;
        return true;
    }
};

// The options for one slot of catalog vehicle `vehicle` (option 0 = stock): its own parts
// first (modOwnCount of them, stock included), then the wheel sizes or treads every vehicle has.
const std::vector<ModOption>& modOptions(int vehicle, int slot);
int modOwnCount(int vehicle, int slot);
// A part's name the same wherever it's offered ("ENGINE:blazer:2", "TIRES:RACE SLICK"): what
// a career owns. Option 0 of a vehicle's own list is its factory part.
std::string modPartKey(int vehicle, int slot, int option);
bool modIsFactory(int vehicle, int slot, int option);
// What to call a part ("BLAZER: BIG V8"); "" if there's no such part.
std::string modPartTitle(const std::string& key);
// The key of a vehicle's own part (or a tread) called `name` in `slot`, else the first whose name
// contains it; a tread also by its TireType name ("RACE SLICK"). "" if none.
std::string modFindPart(const char* vehicleId, int slot, const char* name);
// Catalog vehicle with these mods fitted. The same mods always give the same object
// (cars keep a pointer to their spec); no mods gives the catalog entry itself.
const VehicleSpec& tunedSpec(int vehicle, const ModSet& mods);
// Makes the body take the parts fitted to a tuned copy (trimmed, flared arches and wheel spacers
// for bigger tires and longer travel); tunedSpec does it to every build.
void fitBody(VehicleSpec& s, const VehicleSpec& stock);

// ---- Saves from before parts list 2 ----
// Saves name a vehicle's fitted parts by their place in each slot's list (garage.txt, the career
// profile's vehicles, a car's condition text). Parts list 2 cut the lists down (every vehicle used to
// offer every other vehicle's parts too), so a save says which list it counts in ("parts 2"); one
// that doesn't is from before, and its picks go through modLegacyMods.
constexpr int kPartsVersion = 2;
// The part an old pick named ("ENGINE:jesko:1", "TIRES:SNOW"); "" if out of range.
std::string modLegacyKey(int vehicle, int slot, int oldPick);
// Today's option nearest an old pick: the same part if it's still there; a cut part's nearest one
// (an upgrade stays an upgrade); another vehicle's engine: this one's nearest in power; its wheels:
// the nearest size; its gearbox or suspension, or a cut tread's nearest one on the factory tread: 0.
int modLegacyPick(int vehicle, int slot, int oldPick);
ModSet modLegacyMods(int vehicle, const ModSet& old);
// An owned part's key today: a cut part's goes to its nearest one ("TIRES:SNOW" -> "TIRES:ALL-TERRAIN");
// any other comes back as it is.
std::string modMigrateKey(const std::string& key);

// Which mods are fitted to each vehicle in your garage: keys 1-4, parked spawns and
// traffic all use these. Saved between runs.
struct Garage {
    std::vector<ModSet> fitted;
    std::vector<CarPaint> paint;  // per vehicle (factory unless painted)
    Garage();
    const VehicleSpec& spec(int vehicle) const { return tunedSpec(vehicle, fitted[vehicle]); }
    bool load(const std::string& path);  // (an old file's picks: modLegacyMods)
    bool save(const std::string& path) const;
    static std::string defaultPath();  // ~/.config/crumplezone/garage.txt
};
