// Career mode (career.h): what you own, what you've found, what you've earned.
#include "career.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "mods.h"
#include "platform.h"
#include "world.h"

namespace {

// A cache's contents: a vehicle's own part by name (or a tread: vehicle "", slot MOD_TIRES),
// or cash.
struct Loot {
    const char* id;
    const char* feature;
    double dx, dz;  // off the feature (m): beside the trail, not on it
    const char* vehicle;
    int slot;
    const char* part;
    int cash;
};

// Out where it takes some getting to: the harder the place, the better the find.
const Loot kFrontier[] = {
    {"THE WALL TOP", "walltop", 4, 3, "blazer", MOD_ENGINE, "454 BIG BLOCK", 0},
    {"SUMMIT", "summit", 5, -4, "", MOD_TIRES, "RACE SLICK", 0},
    {"FROZEN TARN", "tarn", -6, 4, "", MOD_TIRES, "SNOW", 0},
    {"MUD HILL", "mudhill", 5, 5, "", MOD_TIRES, "MUD-TERRAIN", 0},
    {"THE WALLOW", "wallow2", 4, -4, "bronco", MOD_SUSPENSION, "3 IN LIFT AND 37S", 0},
    {"MESA STAIRS", "mesaclimb", 3, 6, "4runner", MOD_TRANSMISSION, "4.88 AND LOCKERS", 0},
    {"ABOVE THE LODGE", "lodge", 30, -26, "tacoma", MOD_ENGINE, "MAGNUSON SUPERCHARGER", 0},
    {"SLICKROCK BOWL", "bowl", 4, 4, nullptr, 0, nullptr, 1500},
    {"MUD DELTA", "delta", -5, 5, nullptr, 0, nullptr, 800},
    {"ECHO LAKE BEACH", "lake", -78, 34, nullptr, 0, nullptr, 1000},
};
const Loot kBackcountry[] = {
    {"OLD MINE", "mine", 4, 4, "blazer", MOD_ENGINE, "ZZ4 CRATE 350", 0},
    {"CLIFF OVERLOOK", "overlook", 4, -3, "buggy", MOD_ENGINE, "7.0 LS7", 0},
    {"HEARTBREAK HILL", "hilltop", 4, 4, "blazer", MOD_TRANSMISSION, "4.56 GEARS AND LOCKERS", 0},
    {"THE SINK", "bog3", 5, -5, "", MOD_TIRES, "MUD-TERRAIN", 0},
    {"WRECK SITE", "wreck", 4, 4, "4runner", MOD_SUSPENSION, "LONG TRAVEL AND 37S", 0},
    {"FIRE TOWER", "towerlook", 5, 4, "", MOD_TIRES, "ALL-TERRAIN", 0},
    {"DEEP CROSSING", "deepbank", -4, -4, "tacoma", MOD_TRANSMISSION, "CRAWL BOX", 0},
    {"SLAB POINT", "slab", 4, 4, nullptr, 0, nullptr, 1200},
    {"DUNE BOWL", "dunering", -5, 5, nullptr, 0, nullptr, 900},
    {"TRAPPER'S CABIN", "cabinend", 5, 5, nullptr, 0, nullptr, 2000},
};

std::string lootKey(const Loot& l) {
    if (!l.part) return "";
    if (l.slot == MOD_TIRES && !*l.vehicle) return std::string("TIRES:") + l.part;
    return modFindPart(l.vehicle, l.slot, l.part);
}

// The best parts: a level's first gold gives one you haven't got.
const char* kGoldParts[][3] = {{"jesko", "0", "RACE FUEL AND BOOST"}, {"buggy", "0", "BLOWN 6.2"}, {"civic", "0", "B18C TYPE R SWAP"},
                               {"jesko", "2", "TRACK DIFF MAP"},      {"bronco", "1", "SASQUATCH PACKAGE"}, {"utv", "0", "STAGE 2 TUNE"}};

const char* kMedalName[] = {"", "BRONZE", "SILVER", "GOLD"};
const int kMedalPay[] = {0, 400, 800, 1500};

}  // namespace

std::vector<CacheSpot> cacheSpots(const World& world) {
    std::vector<CacheSpot> out;
    const Loot* list = nullptr;
    int n = 0;
    if (world.map == MAP_FRONTIER) list = kFrontier, n = sizeof(kFrontier) / sizeof(kFrontier[0]);
    else if (world.map == MAP_BACKCOUNTRY) list = kBackcountry, n = sizeof(kBackcountry) / sizeof(kBackcountry[0]);
    for (int i = 0; i < n; i++) {
        const Loot& l = list[i];
        const V3 f = (world.map == MAP_FRONTIER ? frontierFeature(l.feature) : backcountryFeature(l.feature)) + V3(l.dx, 0, l.dz);
        // (On dry ground: out of the bog or the river, the nearest spot above the water.)
        V3 p = f;
        auto dry = [&](const V3& q) { return world.waterLevel(q.x, q.z) < world.height(q.x, q.z) - 0.3; };
        for (double r = 3; !dry(p) && r <= 60; r += 3)
            for (int a = 0; a < 12; a++) {
                const V3 q = f + V3(std::cos(a * M_PI / 6), 0, std::sin(a * M_PI / 6)) * r;
                if (dry(q)) {
                    p = q;
                    break;
                }
            }
        p.y = world.height(p.x, p.z);
        out.push_back({l.id, world.map, p, lootKey(l), l.cash});
    }
    return out;
}

bool Career::owns(int vehicle, int slot, int option) const {
    const std::string key = modPartKey(vehicle, slot, option);
    if (key.empty()) return false;
    if (!on || modIsFactory(vehicle, slot, option)) return true;
    return owned.count(key) > 0;
}

int Career::tier(int vehicle, int slot, int option) {
    if (modPartKey(vehicle, slot, option).empty()) return 3;
    const ModOption& o = modOptions(vehicle, slot)[option];
    if (o.fromVehicle < 0)  // a tread
        return o.fromOption == TIRE_RACE_SLICK ? 3 : o.fromOption == TIRE_SEMI_SLICK || o.fromOption == TIRE_MUD_TERRAIN ? 2 : 1;
    if (slot == MOD_ENGINE) {
        ModSet ms;
        ms.pick[MOD_ENGINE] = o.fromOption;
        const double hp = peakPower(tunedSpec(o.fromVehicle, ms)) / 745.7;
        return hp < 220 ? 1 : hp < 480 ? 2 : 3;
    }
    return o.fromOption == 0 ? 1 : 2;
}

int Career::price(int vehicle, int slot, int option) {
    static const int base[MOD_SLOTS] = {2500, 1500, 1200, 800, 500};
    const int t = tier(vehicle, slot, option);
    return t == 1 ? base[slot] : t == 2 ? base[slot] * 5 / 2 : 0;
}

bool Career::buy(int vehicle, int slot, int option, std::string& why) {
    if (owns(vehicle, slot, option)) return why = "YOU HAVE IT ALREADY", false;
    const int p = price(vehicle, slot, option);
    if (p <= 0) return why = "NOT FOR SALE: FIND IT OUT THERE", false;
    if (cash < p) return why = "NOT ENOUGH CASH", false;
    cash -= p;
    owned.insert(modPartKey(vehicle, slot, option));
    why = "BOUGHT";
    return true;
}

std::string Career::grant(const std::string& key) {
    if (!key.empty()) owned.insert(key);
    const std::string t = modPartTitle(key);
    return t.empty() ? key : t;
}

std::string Career::openCache(const CacheSpot& c) {
    if (isOpened(c)) return "";
    opened.insert(std::to_string(c.map) + ":" + c.id);
    if (!c.part.empty()) return "FOUND: " + grant(c.part);
    cash += c.cash;
    return "FOUND: $" + std::to_string(c.cash);
}

std::string Career::jobDone() {
    cash += kJobPay;
    return "PAID $" + std::to_string(kJobPay);
}

std::string Career::medal(const std::string& level, int m) {
    std::string said;
    for (int k = 1; k <= std::min(m, 3); k++) {
        const std::string key = "LEVEL:" + level + ":" + kMedalName[k];
        if (claimed.count(key)) continue;
        claimed.insert(key);
        cash += kMedalPay[k];
        said += std::string(said.empty() ? "" : "  ") + kMedalName[k] + " $" + std::to_string(kMedalPay[k]);
        if (k == 3)
            for (const auto& g : kGoldParts) {
                const std::string part = modFindPart(g[0], std::atoi(g[1]), g[2]);
                if (part.empty() || owned.count(part)) continue;
                said += "  AND " + grant(part);
                break;
            }
    }
    return said;
}

void Career::startFresh() {
    cash = kStartCash;
    owned.clear(), opened.clear(), claimed.clear();
}

bool Career::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    owned.clear(), opened.clear(), claimed.clear();
    cash = 0;
    std::string line;
    while (std::getline(f, line)) {
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string k = line.substr(0, sp), v = line.substr(sp + 1);
        if (k == "cash") cash = std::atoi(v.c_str());
        else if (k == "own") owned.insert(v);
        else if (k == "cache") opened.insert(v);
        else if (k == "claim") claimed.insert(v);
    }
    return true;
}

bool Career::save(const std::string& path) const {
    const std::string dir = path.substr(0, path.find_last_of('/'));
    if (!dir.empty()) makeDir(dir);
    std::ofstream f(path);
    if (!f) return false;
    f << "cash " << cash << '\n';
    for (const auto& k : owned) f << "own " << k << '\n';
    for (const auto& k : opened) f << "cache " << k << '\n';
    for (const auto& k : claimed) f << "claim " << k << '\n';
    return (bool)f;
}

std::string Career::defaultPath() { return configDir() + "/career.txt"; }
