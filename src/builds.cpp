#include "builds.h"

#include <cstdio>
#include <cstring>

// Parts are named by donor ("ENGINE:jesko:2" is the Jesko's third engine, whatever vehicle it goes into), so a
// build works on every vehicle. A slot left nullptr keeps whatever the player had picked; that is how a build
// leaves the factory suspension and wheels alone, since a vehicle's own part has a different key on each one.
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
         {"ENGINE:jesko:2",         // RACE FUEL AND BOOST: ~1750 hp, the Jesko's strongest engine
          nullptr,
          "TRANSMISSION:jesko:2",   // TOP SPEED GEARING: the 9-speed with the 3.0 final drive that lets 1750 hp run
          nullptr,
          "TIRES:RACE SLICK"}},     // the most grip there is, for a hypercar's power
        {"SLEEPER", "A BIG ENGINE, EVERYTHING ELSE AS YOU HAD IT",
         {"ENGINE:buggy:2",         // BLOWN 6.2: ~650 hp, the biggest engine after the HYPERCAR SWAP's. Nothing else is named:
          nullptr, nullptr, nullptr, // the suspension and wheels stay as picked (the factory's, if untouched), so the car looks stock
          nullptr}},
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
        // The vehicle's own tread isn't listed again: option 0 is that tread.
        if (found < 0 && s == MOD_TIRES && std::string(b.parts[s]) == std::string("TIRES:") + tireTypeName(cat[vehicle].tireType)) found = 0;
        if (found < 0) {
            std::fprintf(stderr, "builds: %s: %s isn't a part %s can take\n", b.name, b.parts[s], cat[vehicle].id);
            ok = false;
            continue;
        }
        io.pick[s] = found;
    }
    return ok;
}
