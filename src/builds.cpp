#include "builds.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

// Parts are named by donor ("ENGINE:jesko:2" is the Jesko's third engine, whatever vehicle it goes into), so a
// build works on every vehicle. A slot left nullptr keeps whatever the player had picked. "<SLOT>:own"
// ("SUSPENSION:own") names the vehicle's own part, option 0, which has a different key on each vehicle.
const std::vector<Build>& builds() {
    static const std::vector<Build> b = {
        {"ROCK CRAWLER", "LIFT, MUD-TERRAINS AND A CRAWL BOX",
         {nullptr,
          "SUSPENSION:bronco:2",    // 3 IN LIFT AND 37S: +75 mm, softer springs, long travel, 37 in mud tires
          "TRANSMISSION:4runner:3", // CRAWL BOX: 4.88 axles, lockers and a 4.7:1 low range: short gearing with the diffs locked
          nullptr,
          "TIRES:MUD-TERRAIN"}},    // lugs that bite loose rock and clear mud
        {"DESERT RUNNER", "LONG TRAVEL AND BIG ALL-TERRAINS",
         {nullptr,
          "SUSPENSION:baja:1",    // DESERT RACE AND 33S: +40 mm, longer trailing arms, more bump and droop, 33 in all-terrains.
                                  // (Not the Tacoma's long-travel kit: it comes with mud tires, which a build naming a
                                  // vehicle's own tread would leave on, since option 0 applies no tread.)
          nullptr, nullptr,
          "TIRES:ALL-TERRAIN"}},  // the knobby all-rounder for trails and gravel, kept off the mud lugs that sink in sand
        {"RALLY", "RALLY SUSPENSION, CLOSE GEARS, GRAVEL TIRES",
         {nullptr,
          "SUSPENSION:civic:3",     // RALLY KIT: +30 mm and softer springs, sold with gravel tires
          "TRANSMISSION:civic:2",   // CLOSE RATIO AND LSD: the short gaps between gears a stage wants, and the diff that puts power down
          nullptr,
          "TIRES:ALL-TERRAIN"}},    // the gravel tread (see DESERT RUNNER)
        {"HYPERCAR SWAP", "THE JESKO'S TOP ENGINE AND GEARBOX, ON SLICKS",
         {"ENGINE:jesko:2",         // RACE FUEL AND BOOST: 2015 hp (peakPower, measured over the catalog), the Jesko's strongest engine
          nullptr,
          "TRANSMISSION:jesko:2",   // TOP SPEED GEARING: the 9-speed with the 3.0 final drive that lets 2015 hp run
          nullptr,
          "TIRES:RACE SLICK"}},     // the most grip there is, for a hypercar's power
        {"SLEEPER", "THE BIGGEST ENGINE, FACTORY SUSPENSION AND WHEELS",
         {"ENGINE:jesko:2",           // RACE FUEL AND BOOST: 2015 hp (peakPower, measured over the catalog), the most of any engine (the HYPERCAR SWAP's too)
          "SUSPENSION:own",           // the vehicle's own suspension and wheels, whatever was picked before: it looks factory
          nullptr,
          "WHEELS:own",
          nullptr}},                  // (the tread stays as picked)
    };
    return b;
}

bool resolveBuild(int vehicle, const Build& b, ModSet& io) {
    const auto& cat = vehicleCatalog();
    if (vehicle < 0 || vehicle >= (int)cat.size()) return false;
    bool ok = true;
    for (int s = 0; s < MOD_SLOTS; s++) {
        if (!b.parts[s]) continue;
        const int n = (int)modOptions(vehicle, s).size();
        int found = -1;
        for (int o = 0; o < n && found < 0; o++)
            if (modPartKey(vehicle, s, o) == b.parts[s]) found = o;
        // "<SLOT>:own" is the vehicle's own part, option 0.
        if (found < 0 && std::string(b.parts[s]) == std::string(modSlotName(s)) + ":own") found = 0;
        // The vehicle's own tread isn't listed again: option 0 is that tread.
        if (found < 0 && s == MOD_TIRES && std::string(b.parts[s]) == std::string("TIRES:") + tireTypeName(cat[vehicle].tireType)) found = 0;
        if (found < 0) {
            // (Once per key per process: the picker resolves builds as it needs them, and the same bad key would repeat.)
            static std::mutex told_m;
            static std::set<std::string> told;
            bool first;
            {
                std::lock_guard<std::mutex> lk(told_m);
                first = told.insert(std::string(b.name) + "|" + b.parts[s] + "|" + cat[vehicle].id).second;
            }
            if (first) std::fprintf(stderr, "builds: %s: %s isn't a part %s can take\n", b.name, b.parts[s], cat[vehicle].id);
            ok = false;
            continue;
        }
        io.pick[s] = found;
    }
    return ok;
}
