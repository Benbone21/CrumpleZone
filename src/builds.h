// The showroom's curated builds: named combinations of existing parts (no mod data of their own).
#pragma once
#include <vector>

#include "mods.h"

struct Build {
    const char* name;
    const char* detail;
    const char* parts[MOD_SLOTS];  // part keys (modPartKey), one per slot in ModSlot order; nullptr: the build doesn't name that slot
};

const std::vector<Build>& builds();

// Sets each slot the build names to the option on `vehicle` with that key; the other slots stay as `io` has them.
// A vehicle's own tread resolves to option 0 (modOptions doesn't list it twice). A key that doesn't resolve leaves
// its slot, prints one line to stderr, and makes this return false.
bool resolveBuild(int vehicle, const Build& b, ModSet& io);
