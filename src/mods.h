// Car mods: engine, suspension and transmission upgrades for each vehicle.
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

enum ModSlot { MOD_ENGINE, MOD_SUSPENSION, MOD_TRANSMISSION, MOD_SLOTS };
const char* modSlotName(int slot);

struct ModOption {
    std::string name;    // "STAGE 1 TUNE"
    std::string detail;  // what it changes, in a few words
    std::function<void(VehicleSpec&)> apply;
};

struct ModSet {
    int pick[MOD_SLOTS] = {0, 0, 0};
    bool stock() const { return !pick[0] && !pick[1] && !pick[2]; }
};

// The options for one slot of catalog vehicle `vehicle` (option 0 = stock).
const std::vector<ModOption>& modOptions(int vehicle, int slot);
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
