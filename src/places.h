// Places to find on a map (docs/plans/big-maps.md, phase 7): outposts (a ranger station, fuel stops, cabins and old camps),
// lookouts, waterfalls, mines, summits, river crossings, ruins, photo spots, supply caches and abandoned vehicles.
//
// One table per map, in code, with no world needed to read it (the map builder places the buildings from it, the M map and the
// discovery banner read it, and the career reads it for caches, outposts and vehicles). A place's `id` never changes once it has
// shipped (saves refer to it); its `name` is what the game shows ("DISCOVERED: <NAME>"). New places go at the END of a map's table.
#pragma once
#include <string>
#include <vector>

#include "mathx.h"

class World;

enum PlaceKind : int {
    PK_RANGER_STATION,
    PK_FUEL_STOP,
    PK_CABIN,
    PK_OLD_CAMP,
    PK_LOOKOUT,
    PK_WATERFALL,
    PK_MINE,
    PK_SUMMIT,
    PK_RIVER_CROSSING,
    PK_RUIN,
    PK_PHOTO_SPOT,
    PK_CACHE,
    PK_ABANDONED_VEHICLE,
    PK_COUNT
};
const char* placeKindName(int kind);  // "RANGER STATION"
char placeKindGlyph(int kind);        // the letter on the M map's marker

// What an outpost offers (MapPlace::services).
enum PlaceService : unsigned {
    SVC_REPAIR = 1,  // a repair bay
    SVC_SHOP = 2,    // tools and parts for sale
    SVC_FUEL = 4,
    SVC_JOBS = 8,    // a job board
    SVC_GARAGE = 16, // vehicles stored
    SVC_DEALER = 32, // vehicles sold
    SVC_TOW = 64,    // a tow destination
    SVC_SAVE = 128,  // a save point
    SVC_PARTS = 256  // a small parts shelf
};

struct MapPlace {
    std::string id;      // "BC.RANGER": stable
    std::string name;    // "ELK RANGER STATION"
    int kind = PK_CABIN;
    int map = 0;
    V3 p;                // x, z; y is the ground (ask the world)
    double yaw = 0;      // which way it faces (the game's yaw: 0 = +X)
    double radius = 30;  // how near counts as finding it (m)
    double pad = 0;      // the flat ground its buildings stand on (m from p); 0: nothing is built
    const char* region = "";
    unsigned services = 0;  // outposts
    int reward = 0;         // a cache's cash; a photo spot's pay
    // A photo spot: p is where the vehicle stands (facing `yaw`), `subject` what the picture is of (its height above the ground).
    V3 subject;
    double subjectY = 0;
    // An abandoned vehicle: the vehicle's id (vehicles.h) and how it's found.
    std::string vehicle, condition;
};

// The map's places in a fixed order (empty for a map with none).
const std::vector<MapPlace>& mapPlaces(int map);
const MapPlace* findPlace(int map, const std::string& id);
const MapPlace* findPlaceByName(int map, const std::string& name);
std::vector<const MapPlace*> placesOfKind(int map, int kind);

// The district a point is in ("" off a map with districts).
const char* regionAt(int map, double x, double z);
std::vector<std::string> regionNames(int map);

// What finding a place pays in a career ($150-$600), by what it is.
int placePay(const MapPlace& p);

// A photo spot's camera: behind and beside the vehicle, looking past it at the subject.
void photoView(const MapPlace& spot, const World& w, V3& eye, V3& target);

// ---- Backcountry geometry the map builder and the tests share ----
// The way up to the Lost Mine: from the wash floor, through a dry falls (rock steps), onto the mesa where the mine is.
const std::vector<V3>& lostMineSpur();

// ---- Discovery ----
// What counts as finding a place: near enough to its spot.
struct DiscoverSpot {
    std::string name;
    V3 p;
    double radius = 30;
    int kind = -1;  // PlaceKind, or -1 for a plain named spot
    // A spot of one of the big map's regions (onemap.h): found, it's kept under the old map it was on ("1:TRAILHEAD", as before),
    // and `label` is what's shown ("MOUNTAIN: TRAILHEAD"). -1: the map it's on.
    int map = -1;
    std::string label;
    int keyMap(int current) const { return map >= 0 ? map : current; }
    const std::string& shown() const { return label.empty() ? name : label; }
};
// The places on a map that are found by getting near (everything but caches and photo spots).
std::vector<DiscoverSpot> discoverSpots(int map);
// Marks every spot within its radius of `at` as found in `found` ("MAP:NAME" lines); returns the ones newly found (each once).
std::vector<std::string> visitSpots(std::vector<std::string>& found, int map, const V3& at, const std::vector<DiscoverSpot>& spots);
bool isFound(const std::vector<std::string>& found, int map, const std::string& name);
// What a player sends so the others in the game have found it too ("found MAP NAME"), and what a receiver does with one: whether it
// was a new place on this map (name filled in). Names are checked against `valid` (the game's list of spots), so a message can't make up a place.
std::string foundMessage(int map, const std::string& name);
bool takeFoundMessage(std::vector<std::string>& found, int currentMap, const std::vector<DiscoverSpot>& valid, const std::string& text, std::string& name);
