// Car mods: engine, suspension, transmission, wheels and tires for each vehicle, and every
// other vehicle's parts too (the Jesko's V8 in the Blazer, the buggy's axles under the Civic).
//
// Every mod is a change to the vehicle's spec (torque curve, ride height, spring
// rates, tires, gear ratios, diffs...), and the car is rebuilt from the modified spec,
// so a lift kit really raises the body on the springs and bigger tires really change
// the gearing. Option 0 of every slot is the factory part.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "vehicles.h"

enum ModSlot { MOD_ENGINE, MOD_SUSPENSION, MOD_TRANSMISSION, MOD_WHEELS, MOD_TIRES, MOD_SLOTS };
const char* modSlotName(int slot);

struct ModOption {
    std::string name;    // "STAGE 1 TUNE"
    std::string detail;  // what it changes, in a few words
    std::function<void(VehicleSpec&)> apply;
    // Where the part comes from: catalog vehicle and its own option index (a tread: vehicle
    // -1 and the TireType). The same part on any vehicle has the same origin.
    int fromVehicle = -1, fromOption = 0;
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
// first (modOwnCount of them, stock included), then every other vehicle's.
const std::vector<ModOption>& modOptions(int vehicle, int slot);
int modOwnCount(int vehicle, int slot);
// A part's name the same wherever it's offered ("ENGINE:blazer:2", "TIRES:RACE SLICK"): what
// a career owns. Option 0 of a vehicle's own list is its factory part.
std::string modPartKey(int vehicle, int slot, int option);
bool modIsFactory(int vehicle, int slot, int option);
// What to call a part ("BLAZER: 454 BIG BLOCK"); "" if there's no such part.
std::string modPartTitle(const std::string& key);
// The key of a vehicle's own part whose name contains `name` in `slot` (a tread by its
// TireType name, e.g. "RACE SLICK"); "" if none.
std::string modFindPart(const char* vehicleId, int slot, const char* name);
// Catalog vehicle with these mods fitted. The same mods always give the same object
// (cars keep a pointer to their spec); no mods gives the catalog entry itself.
const VehicleSpec& tunedSpec(int vehicle, const ModSet& mods);

// Which mods are fitted to each vehicle in your garage: keys 1-4, parked spawns and
// traffic all use these. Saved between runs.
struct Garage {
    std::vector<ModSet> fitted;
    Garage();
    const VehicleSpec& spec(int vehicle) const { return tunedSpec(vehicle, fitted[vehicle]); }
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    static std::string defaultPath();  // ~/.config/crumplezone/garage.txt
};
