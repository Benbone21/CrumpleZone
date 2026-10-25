// Career mode (career.h): what you own, what you've found, what you've earned.
#include "career.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "mods.h"
#include "platform.h"
#include "vehicles.h"
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

// The trail-repair gear in the i-th cache of a map: the tools spread over the first few
// (each map has a full set to find), a part or two in every one.
std::vector<std::string> cacheGear(int i) {
    static const Tool tools[] = {TOOL_RATCHET, TOOL_HAMMER, TOOL_WRENCH, TOOL_PRY_BAR, TOOL_TIRE_IRONS};
    static const RepairPart parts[] = {PART_CV_AXLE, PART_TIE_ROD, PART_TIRE, PART_CONTROL_ARM, PART_SPARK_PLUGS, PART_DRIVE_SHAFT};
    std::vector<std::string> g;
    if (i < 5) g.push_back(toolKey(tools[i]));
    g.push_back(repairPartKey(parts[i % 6]));
    if (i % 3 == 2) g.push_back(repairPartKey(parts[(i + 2) % 6]));
    // (A flooded engine wants oil as well as plugs: it comes with them.)
    if (parts[i % 6] == PART_SPARK_PLUGS || (i % 3 == 2 && parts[(i + 2) % 6] == PART_SPARK_PLUGS)) g.push_back(repairPartKey(PART_ENGINE_OIL));
    // And anchor gear: a stake in a third of them, the scarce kinds where the ground calls for them.
    if (i % 3 == 0) g.push_back(anchors::gearKey(GEAR_STAKE));
    if (i == 4) g.push_back(toolKey(TOOL_JUMP_PACK));  // (one pack and one set of cables to find on each map)
    if (i == 7) g.push_back(toolKey(TOOL_CABLES));
    if (i == 2 || i == 6) g.push_back(anchors::gearKey(GEAR_ICE));
    if (i == 3 || i == 7) g.push_back(anchors::gearKey(GEAR_BOLT));
    return g;
}

// The proving ground's: three crates by the model yard, to try opening one (no feature: dx, dz
// are where they are).
const Loot kProving[] = {
    {"YARD CRATE A", nullptr, -22, -28, "", MOD_TIRES, "MUD-TERRAIN", 0},
    {"YARD CRATE B", nullptr, -22, -52, "blazer", MOD_ENGINE, "454 BIG BLOCK", 0},
    {"YARD CRATE C", nullptr, -22, -76, nullptr, 0, nullptr, 1000},
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
    else if (world.map == MAP_PROVING_GROUND) list = kProving, n = sizeof(kProving) / sizeof(kProving[0]);
    for (int i = 0; i < n; i++) {
        const Loot& l = list[i];
        const V3 f = (!l.feature ? V3() : world.map == MAP_FRONTIER ? frontierFeature(l.feature) : backcountryFeature(l.feature)) + V3(l.dx, 0, l.dz);
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
        out.push_back({l.id, world.map, p, lootKey(l), l.cash, cacheGear(i)});
    }
    return out;
}

std::vector<std::string> partSources(const std::string& key) {
    std::vector<std::string> out;
    if (key.empty()) return out;
    const struct { const Loot* l; int n; int map; } maps[] = {{kFrontier, (int)(sizeof(kFrontier) / sizeof(kFrontier[0])), MAP_FRONTIER},
                                                              {kBackcountry, (int)(sizeof(kBackcountry) / sizeof(kBackcountry[0])), MAP_BACKCOUNTRY},
                                                              {kProving, (int)(sizeof(kProving) / sizeof(kProving[0])), MAP_PROVING_GROUND}};
    for (const auto& m : maps)
        for (int i = 0; i < m.n; i++)
            if (lootKey(m.l[i]) == key) out.push_back(std::string("SUPPLY CACHE: ") + m.l[i].id + " (" + mapName(m.map) + ")");
    for (const auto& g : kGoldParts)
        if (modFindPart(g[0], std::atoi(g[1]), g[2]) == key) out.push_back("A LEVEL'S FIRST GOLD MEDAL");
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
    if (key.rfind("REPAIR:", 0) == 0 || key.rfind("ANCHOR:", 0) == 0) {
        stock[key]++;
        return key.substr(7);
    }
    if (key.rfind("TOOL:", 0) == 0) {
        owned.insert(key);
        return key.substr(5);
    }
    if (!key.empty()) owned.insert(key);
    const std::string t = modPartTitle(key);
    return t.empty() ? key : t;
}

std::string Career::openCache(const CacheSpot& c) {
    if (isOpened(c)) return "";
    opened.insert(std::to_string(c.map) + ":" + c.id);
    std::string said;
    if (!c.part.empty()) said = "FOUND: " + grant(c.part);
    else cash += c.cash, said = "FOUND: $" + std::to_string(c.cash);
    for (const std::string& g : c.gear) said += ", " + grant(g);
    return said;
}

Kit Career::kit() const {
    Kit k;
    k.unlimited = !on;
    for (int t = 0; t < TOOL_COUNT; t++) k.tools[t] = owned.count(toolKey((Tool)t)) > 0;
    for (int p = 0; p < PART_COUNT; p++) {
        auto it = stock.find(repairPartKey((RepairPart)p));
        k.parts[p] = it == stock.end() ? 0 : it->second;
    }
    return k;
}

void Career::usePart(RepairPart p) {
    auto it = stock.find(repairPartKey(p));
    if (it != stock.end() && --it->second <= 0) stock.erase(it);
}

AnchorStock Career::anchorStock() const {
    AnchorStock a;
    if (!on) return a;
    for (int g = 0; g < GEAR_COUNT; g++) {
        auto it = stock.find(anchors::gearKey((AnchorGear)g));
        a.n[g] = it == stock.end() ? 0 : it->second;
    }
    a.refills = false;
    return a;
}

void Career::setAnchorStock(const AnchorStock& a) {
    for (int g = 0; g < GEAR_COUNT; g++) {
        const std::string k = anchors::gearKey((AnchorGear)g);
        if (a.n[g] > 0) stock[k] = a.n[g];
        else stock.erase(k);
    }
}

const std::vector<ShopItem>& stationRack() {
    static const std::vector<ShopItem> r = [] {
        std::vector<ShopItem> v;
        const int toolPrice[TOOL_COUNT] = {60, 140, 220, 45, 70, 55, 180, 150, 45};
        const int partPrice[PART_COUNT] = {160, 240, 300, 90, 190, 40, 25};
        for (int t = 0; t < TOOL_COUNT; t++) v.push_back({toolKey((Tool)t), toolPrice[t], toolModel((Tool)t)});
        for (int p = 0; p < PART_COUNT; p++) v.push_back({repairPartKey((RepairPart)p), partPrice[p], p == PART_TIRE ? "" : repairPartModel((RepairPart)p)});
        for (int g = 0; g < GEAR_COUNT; g++) v.push_back({anchors::gearKey((AnchorGear)g), anchors::kPrice[g], anchors::gearModel((AnchorGear)g)});
        return v;
    }();
    return r;
}

V3 rackSpot(const World& world, int station, int i) {
    const Station& st = world.stations[station];
    const V3 f(std::cos(st.yaw), 0, -std::sin(st.yaw)), left(-f.z, 0, f.x);
    const int n = (int)stationRack().size();
    V3 p = st.p - left * 5.0 + f * ((i - (n - 1) / 2.0) * 0.75);
    p.y = world.height(p.x, p.z);
    return p;
}

bool Career::buyGear(const ShopItem& it, std::string& why) {
    if (it.key.rfind("TOOL:", 0) == 0 && owned.count(it.key)) return why = "YOU HAVE ONE", false;
    if (cash < it.price) return why = "NOT ENOUGH CASH", false;
    cash -= it.price;
    grant(it.key);
    return true;
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

void Career::startFresh(Difficulty d) {
    cash = startCash(d);
    owned.clear(), opened.clear(), claimed.clear(), stock.clear(), vehicles.clear();
    // You set out with what's in any truck: a lug wrench and a jack.
    owned.insert(toolKey(TOOL_LUG_WRENCH)), owned.insert(toolKey(TOOL_JACK));
    AnchorStock a;
    a.fill(anchors::kCareerStart);
    setAnchorStock(a);
}

namespace {

// A condition is one line in the file: backslash and newline are escaped.
std::string escapeLine(const std::string& s) {
    std::string r;
    for (char c : s) r += c == '\\' ? "\\\\" : c == '\n' ? "\\n" : c == '\r' ? "" : std::string(1, c);
    return r;
}
std::string unescapeLine(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) r += s[++i] == 'n' ? '\n' : s[i];
        else r += s[i];
    }
    return r;
}

// Reads career text; false if it has no "cash" line (not a career file). Keys it doesn't know are skipped.
bool parseCareer(std::istream& f, Career& c) {
    c.owned.clear(), c.opened.clear(), c.claimed.clear(), c.stock.clear(), c.vehicles.clear();
    c.cash = 0;
    bool sawCash = false;
    std::vector<std::pair<size_t, std::string>> conds;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string k = line.substr(0, sp), v = line.substr(sp + 1);
        if (k == "cash") c.cash = std::atoi(v.c_str()), sawCash = true;
        else if (k == "own") c.owned.insert(v);
        else if (k == "cache") c.opened.insert(v);
        else if (k == "claim") c.claimed.insert(v);
        else if (k == "stock") {  // "stock N KEY"
            const size_t sp2 = v.find(' ');
            if (sp2 != std::string::npos) c.stock[v.substr(sp2 + 1)] = std::atoi(v.substr(0, sp2).c_str());
        } else if (k == "veh") {  // "veh INDEX WORLD P0 P1 P2 P3 P4" (the picks as in garage.txt)
            std::istringstream is(v);
            OwnedVehicle ov;
            int p[MOD_SLOTS];
            if (!(is >> ov.vehicle >> ov.world) || ov.vehicle < 0 || ov.vehicle >= (int)vehicleCatalog().size()) continue;
            bool ok = true;
            for (int i = 0; i < MOD_SLOTS; i++) ok &= (bool)(is >> p[i]);
            if (!ok) continue;
            if (ov.world == "-") ov.world.clear();
            for (int i = 0; i < MOD_SLOTS; i++) ov.mods.pick[i] = std::min(std::max(p[i], 0), (int)modOptions(ov.vehicle, i).size() - 1);
            c.vehicles.push_back(ov);
        } else if (k == "cond") {  // "cond N TEXT"
            const size_t sp2 = v.find(' ');
            const int n = std::atoi(v.substr(0, sp2).c_str());
            if (n >= 0) conds.push_back({(size_t)n, sp2 == std::string::npos ? "" : unescapeLine(v.substr(sp2 + 1))});
        }
    }
    for (const auto& cd : conds)
        if (cd.first < c.vehicles.size()) c.vehicles[cd.first].condition = cd.second;
    if (!sawCash) return false;
    // A career from before anchor gear: the starting set (a strap and a sling are never used up, so none means none yet).
    if (!c.stock.count(anchors::gearKey(GEAR_STRAP)) && !c.stock.count(anchors::gearKey(GEAR_SLING))) {
        AnchorStock a;
        a.fill(anchors::kCareerStart);
        c.setAnchorStock(a);
    }
    return true;
}

bool validCareerText(const std::string& text) {
    std::istringstream is(text);
    Career probe;
    return parseCareer(is, probe);
}

std::string careerText(const Career& c) {
    std::ostringstream f;
    f << "cash " << c.cash << '\n';
    for (const auto& k : c.owned) f << "own " << k << '\n';
    for (const auto& k : c.opened) f << "cache " << k << '\n';
    for (const auto& k : c.claimed) f << "claim " << k << '\n';
    for (const auto& k : c.stock) f << "stock " << k.second << ' ' << k.first << '\n';
    return f.str();
}

}  // namespace

bool Career::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    parseCareer(f, *this);
    return true;
}

bool Career::save(const std::string& path) const {
    const std::string dir = path.substr(0, path.find_last_of('/'));
    if (!dir.empty()) makeDir(dir);
    std::ofstream f(path);
    if (!f) return false;
    f << careerText(*this);
    return (bool)f;
}

std::string Career::profilePath() { return careerConfigDir() + "/profile.txt"; }

bool Career::loadProfile() {
    for (const std::string& path : {profilePath(), profilePath() + ".bak"}) {
        std::ifstream f(path);
        if (f && parseCareer(f, *this)) return true;
    }
    return load(defaultPath());  // (the old file stays as it is)
}

bool Career::saveProfile() const {
    makeDir(careerConfigDir());
    std::string text = careerText(*this);
    for (size_t i = 0; i < vehicles.size(); i++) {
        const OwnedVehicle& v = vehicles[i];
        text += "veh " + std::to_string(v.vehicle) + ' ' + (v.world.empty() ? "-" : v.world);
        for (int k = 0; k < MOD_SLOTS; k++) text += ' ' + std::to_string(v.mods.pick[k]);
        text += '\n';
        if (!v.condition.empty()) text += "cond " + std::to_string(i) + ' ' + escapeLine(v.condition) + '\n';
    }
    std::string err;
    return writeFileAtomic(profilePath(), text, &err, true, validCareerText);
}

std::string Career::defaultPath() { return careerConfigDir() + "/career.txt"; }
