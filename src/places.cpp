#include "places.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "onemap.h"
#include "realterrain.h"
#include "rivers.h"
#include "world.h"

const char* placeKindName(int kind) {
    static const char* n[PK_COUNT] = {"RANGER STATION", "FUEL STOP", "CABIN",  "OLD CAMP", "LOOKOUT", "WATERFALL", "MINE",
                                      "SUMMIT",         "CROSSING",  "RUIN",   "PHOTO SPOT", "CACHE",   "ABANDONED VEHICLE"};
    return kind >= 0 && kind < PK_COUNT ? n[kind] : "";
}
char placeKindGlyph(int kind) {
    static const char g[PK_COUNT + 1] = "RFCALWMSXUPKV";
    return kind >= 0 && kind < PK_COUNT ? g[kind] : '?';
}

const char* regionAt(int map, double x, double z) {
    if (map != MAP_BACKCOUNTRY) return "";
    if (const MapRegion* r = regionContaining(x, z)) return r->name;  // (the big map's regions: onemap.h)
    if (const MapRegion* r = regionAround(x, z)) return r->name;      // (...and the country grown round them)
    // Elk Valley is the old 1.2 km map and its margin; round it the eight districts (backcountry.cpp: districtsAt).
    constexpr double a = 800;
    static const char* names[3][3] = {{"LAKESHORE", "SOUTH UPLANDS", "BADLANDS WASH"},
                                      {"RANGER FLATS", "ELK VALLEY", "BEAR CREEK CANYON"},
                                      {"TIMBER BASIN", "COPPER RIDGE", "HIGH LAKES"}};
    const int col = x < -a ? 0 : x > a ? 2 : 1, row = z > a ? 2 : z < -a ? 0 : 1;
    return names[row][col];
}
std::vector<std::string> regionNames(int map) {
    if (map != MAP_BACKCOUNTRY) return {};
    // (The Backcountry's own districts. The big map's regions round it are named by regionAt too, but their places are their old maps':
    // the autopilot's and discovery's lists, not this table.)
    return {"ELK VALLEY", "RANGER FLATS", "TIMBER BASIN", "COPPER RIDGE", "HIGH LAKES", "BEAR CREEK CANYON", "BADLANDS WASH", "SOUTH UPLANDS", "LAKESHORE"};
}

namespace {
constexpr double kPi = M_PI;

// The Lost Mine, on a mesa in Badlands Wash: reached by a spur up a scarp (a dry falls: rock steps in a cut) from the wash floor.
const V3 kLostMine(2262, 0, -1745);

// ---- The supply caches in the new country (the old ones are career.cpp's, and stay as they are) ----
struct CacheRow {
    const char* name;
    double x, z;
    int cash;
};
#include "places_caches.inc"

void add(std::vector<MapPlace>& v, int map, const char* id, const char* name, int kind, V3 p, double yaw, double radius, double pad, unsigned svc = 0) {
    MapPlace m;
    m.id = id, m.name = name, m.kind = kind, m.map = map, m.p = V3(p.x, 0, p.z), m.yaw = yaw, m.radius = radius, m.pad = pad, m.services = svc;
    m.region = regionAt(map, p.x, p.z);
    v.push_back(m);
}

std::vector<MapPlace> backcountryPlaces() {
    const int M = MAP_BACKCOUNTRY;
    std::vector<MapPlace> v;
    const unsigned camp = SVC_TOW | SVC_SAVE, station = SVC_REPAIR | SVC_TOW | SVC_SAVE;
    // -- The old map (Elk Valley): the four camp stations, the mine, the cabins and lookouts, the fords --
    add(v, M, "BC.TRAILHEAD_CAMP", "TRAILHEAD CAMP", PK_OLD_CAMP, backcountryFeature("trailhead"), 0, 35, 0, station);
    add(v, M, "BC.SAWMILL_CAMP", "SAWMILL CAMP", PK_OLD_CAMP, backcountryFeature("sawmill"), kPi / 2, 35, 0, station);
    add(v, M, "BC.FARSIDE_CAMP", "FAR SIDE CAMP", PK_OLD_CAMP, backcountryFeature("farside"), 0, 35, 0, station);
    add(v, M, "BC.PASS_HUT", "PASS HUT", PK_OLD_CAMP, backcountryFeature("passhut"), kPi / 2, 35, 0, station);
    add(v, M, "BC.OLD_MINE", "OLD MINE", PK_MINE, backcountryFeature("mine"), 0, 45, 0);
    add(v, M, "BC.TRAPPERS_CABIN", "TRAPPER'S CABIN", PK_CABIN, backcountryFeature("cabinend"), 0, 35, 0, camp | SVC_PARTS);
    add(v, M, "BC.SLAB_POINT", "SLAB POINT LOOKOUT", PK_LOOKOUT, backcountryFeature("slab"), 0, 40, 0);
    add(v, M, "BC.CLIFF_OVERLOOK", "CLIFF OVERLOOK", PK_LOOKOUT, backcountryFeature("overlook"), 0, 40, 0);
    add(v, M, "BC.FIRE_TOWER", "FIRE TOWER", PK_LOOKOUT, backcountryFeature("towerlook"), 0, 40, 0);
    add(v, M, "BC.WRECK_SITE", "WRECK SITE", PK_RUIN, backcountryFeature("wreckbody") + V3(-6, 0, 4), 0, 40, 0);
    add(v, M, "BC.HEARTBREAK_TOP", "HEARTBREAK HILL (TOP)", PK_SUMMIT, backcountryFeature("hilltop"), 0, 35, 0);
    add(v, M, "BC.ELK_FORD", "ELK RIVER FORD", PK_RIVER_CROSSING, backcountryFeature("fordbank"), 0, 45, 0);
    add(v, M, "BC.DEEP_CROSSING", "DEEP CROSSING", PK_RIVER_CROSSING, backcountryFeature("deepbank"), 0, 45, 0);
    // -- The ranger station, on Ranger Flats: repair, shop, fuel, jobs, a garage and a dealer --
    add(v, M, "BC.RANGER", "ELK RANGER STATION", PK_RANGER_STATION, V3(-1052, 0, 340), kPi / 2, 50, 26,
        SVC_REPAIR | SVC_SHOP | SVC_FUEL | SVC_JOBS | SVC_GARAGE | SVC_DEALER | SVC_TOW | SVC_SAVE);
    // -- Four fuel stops --
    add(v, M, "BC.FUEL_TIMBER", "TIMBER BASIN FUEL", PK_FUEL_STOP, V3(-1374, 0, 1320), kPi / 2, 35, 11, SVC_FUEL | SVC_PARTS | SVC_TOW);
    add(v, M, "BC.FUEL_SOUTH", "SOUTH FORK FUEL", PK_FUEL_STOP, V3(134, 0, -1504), kPi / 2, 35, 11, SVC_FUEL | SVC_PARTS | SVC_TOW);
    add(v, M, "BC.FUEL_RIM", "RIM ROAD FUEL", PK_FUEL_STOP, V3(1514, 0, -330), kPi / 2, 35, 11, SVC_FUEL | SVC_PARTS | SVC_TOW);
    add(v, M, "BC.FUEL_LAKE", "LAKESHORE FUEL", PK_FUEL_STOP, V3(-1248, 0, -1136), kPi / 2, 35, 11, SVC_FUEL | SVC_PARTS | SVC_TOW);
    // -- Cabins and old camps --
    add(v, M, "BC.HERMIT_CABIN", "HERMIT'S CABIN", PK_CABIN, V3(-1250, 0, 1604), kPi / 2, 40, 22, camp | SVC_GARAGE);
    add(v, M, "BC.SURVEYOR_CABIN", "SURVEYOR'S CABIN", PK_CABIN, V3(-78, 0, 1924), kPi / 2, 35, 9, camp | SVC_PARTS);
    add(v, M, "BC.FISHING_SHACK", "FISHING SHACK", PK_CABIN, V3(-1326, 0, -1502), kPi / 2, 35, 9, camp);
    add(v, M, "BC.LOGGERS_CAMP", "LOGGERS' CAMP", PK_OLD_CAMP, V3(-1852, 0, 830), 0, 35, 10, camp);
    add(v, M, "BC.BEAR_CREEK_CAMP", "BEAR CREEK CAMP", PK_OLD_CAMP, V3(1208, 0, -248), kPi / 2, 35, 10, camp);
    add(v, M, "BC.HIGH_LAKES_CAMP", "HIGH LAKES CAMP", PK_OLD_CAMP, V3(1490, 0, 1426), 0, 35, 10, camp);
    add(v, M, "BC.HOMESTEAD", "HOMESTEAD RUINS", PK_RUIN, V3(-592, 0, -1304), 0, 40, 10);
    // -- Lookouts and summits --
    add(v, M, "BC.COPPER_VISTA", "COPPER RIDGE VISTA", PK_LOOKOUT, V3(-332, 0, 1516), kPi / 2, 40, 7);
    add(v, M, "BC.RIM_OVERLOOK", "BEAR CREEK RIM OVERLOOK", PK_LOOKOUT, V3(1492, 0, 116), -kPi / 2, 40, 7);
    add(v, M, "BC.COPPER_SUMMIT", "COPPER RIDGE SUMMIT", PK_SUMMIT, V3(520, 0, 1582), 0, 35, 4);
    add(v, M, "BC.TIMBER_KNOB", "TIMBER KNOB", PK_SUMMIT, V3(-1076, 0, 1328), 0, 35, 4);
    add(v, M, "BC.HIGH_LAKES_PEAK", "HIGH LAKES PEAK", PK_SUMMIT, V3(2092, 0, 1506), 0, 35, 4);
    // -- Waterfalls (the viewing ground by the plunge pool) and the fords of Elk River's new country --
    add(v, M, "BC.BEAR_FALLS", "BEAR FALLS", PK_WATERFALL, V3(1846, 0, 14), 0, 85, 0);
    add(v, M, "BC.UPPER_FALLS", "UPPER FALLS", PK_WATERFALL, V3(2559, 0, -18), 0, 65, 0);
    add(v, M, "BC.RANGER_FORD", "RANGER FORD", PK_RIVER_CROSSING, V3(-810, 0, 10), 0, 45, 0);
    add(v, M, "BC.WILLOW_FORD", "WILLOW FORD", PK_RIVER_CROSSING, V3(-1748, 0, 26), 0, 45, 0);
    add(v, M, "BC.BEAR_CREEK_FORD", "BEAR CREEK FORD", PK_RIVER_CROSSING, V3(796, 0, -25), 0, 45, 0);
    add(v, M, "BC.RIM_FORD", "RIM FORD", PK_RIVER_CROSSING, V3(1598, 0, -24), 0, 45, 0);
    add(v, M, "BC.UPPER_FORD", "UPPER FORD", PK_RIVER_CROSSING, V3(2760, 0, 53), 0, 45, 0);
    // -- The Lost Mine --
    add(v, M, "BC.LOST_MINE", "LOST MINE", PK_MINE, kLostMine, kPi, 50, 18, SVC_TOW);
    // -- Abandoned vehicles (their ids and conditions are for the career to recover them; nothing is parked there yet) --
    auto vehicle = [&](const char* id, const char* name, V3 p, double yaw, const char* veh, const char* cond) {
        add(v, M, id, name, PK_ABANDONED_VEHICLE, p, yaw, 35, 0);
        v.back().vehicle = veh, v.back().condition = cond;
    };
    vehicle("BC.HERMIT_TRUCK", "HERMIT'S TRUCK", V3(-1267, 0, 1609), kPi, "blazer", "DEAD BATTERY, FLAT TIRES, SHUT IN THE GARAGE");
    vehicle("BC.MINE_PIT_UTV", "MINE PIT UTV", kLostMine + V3(-16, 0, -11), 0.6, "utv", "NOSE-DOWN IN THE PIT, BENT FRAME");
    vehicle("BC.SUNK_4RUNNER", "SUNKEN 4RUNNER", V3(-2090, 0, -34), -kPi / 2, "4runner", "HALF SUNK IN THE POOL, WATER IN THE ENGINE");
    vehicle("BC.LAKESIDE_CIVIC", "LAKESIDE CIVIC", V3(-1298, 0, -1638), 0.4, "civic", "OUT OF FUEL, STRIPPED OF ITS PARTS");
    // -- Photo spots: p is where the truck stands, `subject` what the picture is of --
    auto photo = [&](const char* id, const char* name, V3 p, V3 subject, double subjectY, int pay) {
        add(v, M, id, name, PK_PHOTO_SPOT, p, std::atan2(-(subject.z - p.z), subject.x - p.x), 25, 0);
        v.back().subject = V3(subject.x, 0, subject.z), v.back().subjectY = subjectY, v.back().reward = pay;
    };
    photo("BC.PHOTO_BEAR_FALLS", "BEAR FALLS FROM THE POOL", V3(1812, 0, 4), V3(1897, 0, 56), 10, 700);
    photo("BC.PHOTO_UPPER_FALLS", "UPPER FALLS CASCADE", V3(2510, 0, -40), V3(2600, 0, 16), 3, 500);
    photo("BC.PHOTO_ELK_FORD", "ELK RIVER FORD AT DAWN", backcountryFeature("fordbank") + V3(-9, 0, -1), backcountryFeature("ford"), 1, 300);
    photo("BC.PHOTO_HERMIT", "HERMIT'S CABIN IN THE PINES", V3(-1272, 0, 1590), V3(-1250, 0, 1604), 3, 400);
    photo("BC.PHOTO_FIRE_TOWER", "FIRE TOWER AT SUNSET", backcountryFeature("towerlook") + V3(-8, 0, 6), V3(238, 0, 422), 12, 450);
    photo("BC.PHOTO_LAKESHORE", "LAKESHORE AT DUSK", V3(-1326, 0, -1658), V3(-1850, 0, -1650), 0, 600);
    photo("BC.PHOTO_COPPER_RIDGE", "COPPER RIDGE SKYLINE", V3(-350, 0, 1530), V3(-60, 0, 1296), 40, 800);
    photo("BC.PHOTO_BADLANDS", "BADLANDS MESAS", V3(1510, 0, -1496), V3(1700, 0, -1700), 25, 550);
    // -- The caches (appended last: the ids of the first ones never move) --
    for (size_t i = 0; i < sizeof(kCaches) / sizeof(kCaches[0]); i++) {
        char id[24];
        std::snprintf(id, sizeof id, "BC.CACHE.%02d", (int)i + 1);
        add(v, M, id, kCaches[i].name, PK_CACHE, V3(kCaches[i].x, 0, kCaches[i].z), 0, 12, 0);
        v.back().reward = kCaches[i].cash;
    }
    // -- The regions' grown country (docs/plans/regions2.md, onemap.h): an outpost (fuel and a repair bay), a cabin or camp, a lookout
    // on a rock ledge, a fire lookout and caches in each; the Mountain's pass. Marked trails link them (onemapbuild.cpp). --
    const unsigned outpost = SVC_FUEL | SVC_PARTS | SVC_TOW | SVC_REPAIR | SVC_SAVE;
    add(v, M, "OC.PRAIRIE_FUEL", "PRAIRIE FUEL", PK_FUEL_STOP, V3(-3170, 0, 800), kPi / 2, 40, 24, outpost);
    add(v, M, "OC.HOMESTEAD", "MEADOW HOMESTEAD", PK_CABIN, V3(-4200, 0, 1940), 0, 35, 10, camp | SVC_PARTS);
    add(v, M, "OC.NORTHWOODS_CAMP", "NORTHWOODS CAMP", PK_OLD_CAMP, V3(-3380, 0, 2600), kPi / 2, 35, 10, camp);
    add(v, M, "OC.FIRE_TOWER", "PRAIRIE FIRE LOOKOUT", PK_LOOKOUT, V3(-3940, 0, 2340), 0, 40, 7);
    add(v, M, "OC.TABLE_ROCK", "TABLE ROCK", PK_LOOKOUT, V3(-3870, 0, -2270), -kPi / 2, 40, 7);
    add(v, M, "MT.WEST_FORK_FUEL", "WEST FORK FUEL", PK_FUEL_STOP, V3(-2750, 0, -3110), 0, 40, 24, outpost);
    add(v, M, "MT.SNOWLINE_CABIN", "SNOWLINE CABIN", PK_CABIN, V3(-3540, 0, -3100), kPi / 2, 35, 9, camp);
    add(v, M, "MT.HIGH_PASS", "HIGH PASS", PK_SUMMIT, V3(-3850, 0, -2880), 0, 40, 5);
    add(v, M, "MT.EAGLE_LEDGE", "EAGLE LEDGE", PK_LOOKOUT, V3(-640, 0, -4450), kPi / 2, 40, 7);
    add(v, M, "MT.FIRE_TOWER", "SOUTH RANGE FIRE LOOKOUT", PK_LOOKOUT, V3(-1840, 0, -4680), 0, 40, 7);
    add(v, M, "FR.BADLANDS_FUEL", "BADLANDS FUEL", PK_FUEL_STOP, V3(710, 0, -3230), 0, 40, 24, outpost);
    add(v, M, "FR.TABLETOP_MESA", "TABLETOP MESA", PK_LOOKOUT, V3(420, 0, -4020), kPi, 40, 7);
    add(v, M, "FR.LINE_SHACK", "COLD CREEK LINE SHACK", PK_CABIN, V3(3460, 0, -3600), 0, 35, 9, camp | SVC_PARTS);
    add(v, M, "FR.SLICKROCK_SHELF", "SLICKROCK SHELF", PK_LOOKOUT, V3(3680, 0, -3712), -kPi / 2, 40, 7);
    add(v, M, "FR.FIRE_TOWER", "COLD BENCH FIRE LOOKOUT", PK_LOOKOUT, V3(4110, 0, -4450), 0, 40, 7);
    struct RegionCache {
        const char* id;
        const char* name;
        double x, z;
        int cash;
    };
    static const RegionCache kRegionCaches[] = {
        {"OC.CACHE.01", "SPLIT RAIL GATE", -4320, 440, 900},     {"OC.CACHE.02", "HAYSTACK RUIN", -3460, -1080, 800},
        {"OC.CACHE.03", "BIRCH HOLLOW", -4280, 2620, 1100},      {"OC.CACHE.04", "REDROCK ALCOVE", -4380, -2470, 1200},
        {"MT.CACHE.01", "AVALANCHE SHED", -3450, -3530, 1400},   {"MT.CACHE.02", "MINER'S TIN", -2070, -4290, 1000},
        {"MT.CACHE.03", "MARMOT ROCKS", -670, -3310, 900},       {"MT.CACHE.04", "PASS SHELTER", -3720, -2990, 1800},
        {"FR.CACHE.01", "WAGON WHEEL", 120, -3680, 800},         {"FR.CACHE.02", "DRY WELL", 400, -4520, 1000},
        {"FR.CACHE.03", "SNOWBOUND CRATE", 3720, -4100, 1500},   {"FR.CACHE.04", "ARROYO TIN", 2430, -4390, 1100},
    };
    for (const RegionCache& c : kRegionCaches) {
        add(v, M, c.id, c.name, PK_CACHE, V3(c.x, 0, c.z), 0, 12, 0);
        v.back().reward = c.cash;
    }
    return v;
}
}  // namespace

const std::vector<MapPlace>& mapPlaces(int map) {
    static const std::vector<MapPlace> bc = backcountryPlaces();
    static const std::vector<MapPlace> none;
    if (map == MAP_REAL) return realterrain::places();  // (the active real terrain's, from its plan)
    return map == MAP_BACKCOUNTRY ? bc : none;
}
const MapPlace* findPlace(int map, const std::string& id) {
    for (const MapPlace& p : mapPlaces(map))
        if (p.id == id) return &p;
    return nullptr;
}
const MapPlace* findPlaceByName(int map, const std::string& name) {
    for (const MapPlace& p : mapPlaces(map))
        if (p.name == name) return &p;
    return nullptr;
}
std::vector<const MapPlace*> placesOfKind(int map, int kind) {
    std::vector<const MapPlace*> out;
    for (const MapPlace& p : mapPlaces(map))
        if (p.kind == kind) out.push_back(&p);
    return out;
}

int placePay(const MapPlace& p) {
    switch (p.kind) {
        case PK_SUMMIT: return 600;
        case PK_MINE: return 500;
        case PK_WATERFALL: return 400;
        case PK_LOOKOUT: return 250;
        case PK_RUIN: return 250;
        case PK_RANGER_STATION: return 300;
        case PK_CABIN: return 200;
        case PK_ABANDONED_VEHICLE: return 200;
        case PK_PHOTO_SPOT: return p.reward;
        case PK_CACHE: return p.reward;
        default: return 150;  // a fuel stop, a camp, a crossing
    }
}

void photoView(const MapPlace& s, const World& w, V3& eye, V3& target) {
    const V3 d = norm(V3(s.subject.x - s.p.x, 0, s.subject.z - s.p.z)), l(-d.z, 0, d.x);
    const V3 stand(s.p.x, w.height(s.p.x, s.p.z), s.p.z);
    eye = stand - d * 8.0 + l * 2.5;
    eye.y = w.height(eye.x, eye.z) + 1.8;
    target = V3(s.subject.x, w.height(s.subject.x, s.subject.z) + s.subjectY, s.subject.z);
    // (Looking between the truck and the subject: the truck low in the frame, the subject above it.)
    target = (target * 0.6) + (stand + V3(0, 1.4, 0)) * 0.4;
}

const std::vector<V3>& lostMineSpur() {
    static const std::vector<V3> pts = {V3(2420, 0, -1752), V3(2370, 0, -1747), V3(2335, 0, -1745), V3(2310, 0, -1745), V3(2290, 0, -1745), V3(2272, 0, -1745)};
    return pts;
}

// ---- Discovery ----------------------------------------------------------------------------------------

std::vector<DiscoverSpot> discoverSpots(int map) {
    std::vector<DiscoverSpot> out;
    for (const MapPlace& p : mapPlaces(map))
        if (p.kind != PK_CACHE && p.kind != PK_PHOTO_SPOT) out.push_back({p.name, p.p, p.radius, p.kind});
    return out;
}

bool isFound(const std::vector<std::string>& found, int map, const std::string& name) {
    return std::find(found.begin(), found.end(), std::to_string(map) + ":" + name) != found.end();
}

std::vector<std::string> visitSpots(std::vector<std::string>& found, int map, const V3& at, const std::vector<DiscoverSpot>& spots) {
    std::vector<std::string> fresh;
    for (const DiscoverSpot& s : spots)
        if (std::hypot(s.p.x - at.x, s.p.z - at.z) < s.radius && !isFound(found, s.keyMap(map), s.name)) {
            found.push_back(std::to_string(s.keyMap(map)) + ":" + s.name);
            fresh.push_back(s.name);
        }
    return fresh;
}

std::string foundMessage(int map, const std::string& name) { return "found " + std::to_string(map) + " " + name; }

bool takeFoundMessage(std::vector<std::string>& found, int currentMap, const std::vector<DiscoverSpot>& valid, const std::string& text, std::string& name) {
    if (text.rfind("found ", 0) != 0) return false;
    const size_t sp = text.find(' ', 6);
    if (sp == std::string::npos) return false;
    const int map = std::atoi(text.c_str() + 6);
    const std::string n = text.substr(sp + 1);
    bool known = false;  // (a region's spot comes under its old map's id)
    for (const DiscoverSpot& s : valid) known |= s.name == n && s.keyMap(currentMap) == map;
    if (!known || isFound(found, map, n)) return false;
    found.push_back(std::to_string(map) + ":" + n);
    name = n;
    return true;
}
