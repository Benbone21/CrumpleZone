// Real-world terrain (docs/real-terrain.md): a real elevation model (DEM) made into a drivable map, MAP_REAL.
//
//   1. A loader for the files the elevation services hand out: SRTM .hgt (1" and 3", big-endian int16), 16-bit grayscale PNG
//      heightmaps, and GeoTIFF (USGS 3DEP and the like: stripped or tiled, uncompressed, LZW or DEFLATE, predictors 1-3, float32 or
//      int16, ModelTiepoint/ModelPixelScale, geographic or projected in metres). No libraries but stb_image's inflate.
//   2. A sidecar text file (terrain.txt or NAME.terrain.txt) saying how to make it into a map: the file, the crop (pixels, lat/lon
//      bounds, or a centre and a size), metres per pixel where the file doesn't say, vertical scale and exaggeration, sea level,
//      the climate, the snow line, ledges on the rock... (parseSpec).
//   3. A plan made from it with no World (planFor): the land on a coarse grid, its drainage (priority flood, flow accumulation),
//      lakes, rivers and dry washes along the real drainage lines, outposts on flat ground near water, lookouts and summits on the
//      tops, and roads (<= 12%) and trails (steeper) between them by A* over a slope cost. The map's places (places.h) are the
//      plan's, so the M map, discovery and the career read them without building the world.
//   4. World::buildReal (realterrainbuild.cpp): the 1 m grid from the DEM (bicubic, smoothed, fine detail added, ledges on the
//      rock), its ground by slope and height, the plan's water carved in, its trails laid, its places built, trees, bushes and
//      the scanned rocks scattered by slope and height. Kept in the world cache under the file's hash.
//
// Everything that decides the land is plain arithmetic (+ - * / sqrt floor, and the series below for the one cosine a lat/lon
// crop needs): no platform libm in it, so the same file builds the same land (World::landHash) on every platform.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "places.h"

namespace realterrain {

// ---- A DEM ---------------------------------------------------------------------------------------------------------------
struct Dem {
    int w = 0, h = 0;
    std::vector<float> v;  // w * h, row 0 the north edge, west to east along a row; elevations in the file's units (metres)
    // Where it is: pixel (0, 0)'s top-left corner and the size of a pixel. Geographic: degrees (lon0, lat0, dLon, -dLat going
    // south); projected: metres (easting, northing). Unknown: geo = GEO_NONE (then metres per pixel come from the sidecar).
    enum Geo { GEO_NONE, GEO_DEGREES, GEO_METRES } geo = GEO_NONE;
    double x0 = 0, y0 = 0, dx = 0, dy = 0;  // (dy > 0: rows go south by dy)
    bool hasNoData = false;
    double noData = 0;
    std::string format;  // "hgt", "png16", "geotiff"
    double at(int x, int y) const {
        x = x < 0 ? 0 : x >= w ? w - 1 : x, y = y < 0 ? 0 : y >= h ? h - 1 : y;
        return v[(size_t)y * w + x];
    }
};
// A rectangle of pixels to read (x1, y1 exclusive); empty (x1 <= x0): all of it.
struct Window {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};
// What a GeoTIFF is, without reading its pixels.
struct TiffInfo {
    int w = 0, h = 0, bits = 0, sampleFormat = 1, samples = 1, compression = 1, predictor = 1, planar = 1;
    bool bigTiff = false, bigEndian = false, tiled = false;
    int tileW = 0, tileH = 0, rowsPerStrip = 0;
    int modelType = 0;   // GTModelTypeGeoKey: 1 projected, 2 geographic
    int epsg = 0;        // the geographic or projected CRS code, when given
    Dem::Geo geo = Dem::GEO_NONE;
    double x0 = 0, y0 = 0, dx = 0, dy = 0;
    bool pixelIsPoint = false;
    bool hasNoData = false;
    double noData = 0;
};

bool loadHgt(const std::string& path, Dem& out, std::string* err = nullptr);  // (its corner from the name: N38W110.hgt)
bool loadPng16(const std::string& path, Dem& out, std::string* err = nullptr);
bool tiffInfo(const std::string& path, TiffInfo& out, std::string* err = nullptr);
bool loadGeoTiff(const std::string& path, Dem& out, std::string* err = nullptr, Window win = Window());
// Any of the above, by its extension (.hgt, .png, .tif/.tiff); `win` crops (the .hgt and .png are read whole, then cropped).
bool loadDem(const std::string& path, Dem& out, std::string* err = nullptr, Window win = Window());
// Its size (and corner) without the pixels, for working out a crop.
bool demHeader(const std::string& path, Dem& out, std::string* err = nullptr);

// Writers (the tests' synthetic DEMs, and the tool's cropped copy): .hgt (int16, big-endian, square), 16-bit grayscale PNG
// (stored deflate blocks), and GeoTIFF (float32; uncompressed, LZW or DEFLATE-stored; stripped or tiled; predictor 1-3).
bool writeHgt(const std::string& path, int side, const std::vector<int16_t>& v);
bool writePng16(const std::string& path, int w, int h, const std::vector<uint16_t>& v);
struct TiffOptions {
    int compression = 1;  // 1 none, 5 LZW, 8 DEFLATE
    int predictor = 1;    // 1 none, 2 horizontal, 3 floating point
    bool tiled = false;
    int tile = 256;
    bool int16 = false;   // int16 samples (else float32)
    bool bigEndian = false;
};
bool writeGeoTiff(const std::string& path, const Dem& dem, const TiffOptions& opt = TiffOptions());

// The cosine and sine of an angle in degrees by series (the same bits everywhere; |error| < 1e-15).
double cosDeg(double deg);
// How many metres a degree of latitude and of longitude is at latitude `lat` (WGS84).
double metresPerDegLat(double lat);
double metresPerDegLon(double lat);

// ---- How to make a map of one --------------------------------------------------------------------------------------------
enum Climate { CLIMATE_TEMPERATE, CLIMATE_DESERT, CLIMATE_ALPINE };
// A point given in the sidecar as latitude and longitude (degrees).
struct LatLon {
    double lat = 0, lon = 0;
};
// What a sidecar authors on top of the DEM: the real map's named places, its big river and the bridge over it, its named trails
// and the courses driven along them (docs/real-terrain.md "Authored places, rivers, trails and courses"). Their names are as
// written, upper case; "|" separates a line's fields.
//   place KIND | NAME | LAT LON [| YAW]            KIND: town (the start: repair, shop, fuel and the rest), fuel, camp, cabin, lookout
//   river NAME | HALF_WIDTH DEPTH | LAT LON ...    a big river, its line found along the valley floor between the points (unfordable when deep)
//   bridge NAME | HALF_WIDTH | LAT LON LAT LON     a deck from bank to bank, with a road onto each end
//   trail NAME | RATING GRADE | LAT LON ...        a marked trail through the points, routed over the land no steeper than GRADE
//   trail NAME | RATING GRADE | osm WAY ... [from LAT LON] [to LAT LON]   ...or along OpenStreetMap ways (the `osm FILE`'s)
//   obstacle NAME | KIND | node ID | TRAIL | KEY VALUE ...   a named obstacle built on a trail (SpecObstacle)
//   landmark NAME | node ID                        a point named over the land
//   osm FILE                                       OpenStreetMap ways and points (tools/osm_extract.py)
//   osm_rules 1                                    ...and its tracks, attractions, trailheads and viewpoints made trails, obstacles
//                                                  and landmarks by rules (Spec::osmRules)
//   include FILE                                   another file's lines here (a map's hand-authored part)
//   course NAME | TRAIL | DIFFICULTY | VEHICLE | HOURS | ABOUT   a sprint along a trail (VEHICLE: an id such as bronco, or any)
struct SpecPlace {
    int kind = 0;  // PlaceKind (PK_RANGER_STATION for a town)
    bool town = false;
    std::string name;
    LatLon at;
    double yawDeg = 90;  // which way it faces (0 east, 90 north)
};
struct SpecRiver {
    std::string name;
    double halfWidth = 40, depth = 3;
    std::vector<LatLon> pts;
};
struct SpecBridge {
    std::string name;
    double halfWidth = 5;
    LatLon a, b;
};
struct SpecTrail {
    std::string name;
    int rating = 1;          // 0 green, 1 blue, 2 black, 3 red (World::MarkedTrail)
    double grade = 0.3;      // the steepest a step of its route may be
    std::vector<LatLon> pts;
    // Its line from OpenStreetMap (`trail NAME | RATING GRADE | osm WAY WAY ...`): those ways of the sidecar's `osm` file chained in
    // order (the first as OSM draws it, or reversed when its id is negative; each after turned to carry on from the last), and followed
    // as they are (not routed; a gap between two of them, or where the
    // line leaves the map and comes back, is routed). `from LAT LON` / `to LAT LON` after the ways cut it to the points nearest those.
    std::vector<int64_t> osmWays;
    bool hasFrom = false, hasTo = false;
    LatLon from, to;
};
// One of the sidecar's OpenStreetMap ways or points (its `osm FILE`: tools/moab_osm.py's output). Data (c) OpenStreetMap contributors.
struct OsmWay {
    int64_t id = 0;
    std::string name, tags;  // (tags as "key=value ...")
    std::vector<LatLon> pts;
};
struct OsmNode {
    int64_t id = 0;
    std::string name, tags;
    LatLon at;
};
// A named obstacle built on a trail (`obstacle NAME | KIND | node ID (or LAT LON) | TRAIL | KEY VALUE ...`): the land under the trail
// reshaped where it passes the point and scanned rocks laid on it (realterrainbuild.cpp). Each key is optional:
//   climb G L      a pitch of grade G (rise over run; < 0 down) L m long through the point (0: none)
//   steps N R      N ledges R m high (rocks laid along each lip), spread over the pitch (or over `length`)
//   length L       how much of the trail it takes (m; default: the pitch, or 6 m a step)
//   width W        the tread's half-width through it (m, default 3.2)
//   walls H        the rock either side raised H m: a notch or a ravine (rocks lining it); < 0: lowered, a fin
//   camber D       the tread tipped D degrees (> 0: the left side up)
//   crack D W      a crack D m deep and W m wide along the tread (on one wheel track) ...
//   cross          ...or across it, diagonally (a tire drops in)
//   bowl D R       a pothole D m deep and R m across the line (in and out over its lips)
//   boulders N     N big scanned boulders in and beside the line to pick through
//   bypass left|right|TRAIL   a way round it: authored on that side (laid as a trail), or a trail of the sidecar's (an OSM bypass)
// What each is based on is said in a comment beside it in the sidecar.
struct SpecObstacle {
    std::string name, kind, trail, bypass;
    int64_t node = 0;  // the OSM node it's at (0: `at` given)
    LatLon at;
    double climbGrade = 0, climbLen = 0, length = 0, width = 3.2, walls = 0, camber = 0;
    int steps = 0;
    double rise = 0;
    double crackDepth = 0, crackWidth = 0;
    bool crackAcross = false;
    double bowlDepth = 0, bowlSize = 0;
    int boulders = 0;
};
// A point named over the land (`landmark NAME | node ID (or LAT LON)`): a tag shown near it, a cairn at it (a closed trail's sign).
struct SpecLandmark {
    std::string name;
    int64_t node = 0;
    LatLon at;
};
struct SpecCourse {
    std::string name, trail, about, vehicle;  // vehicle: a VehicleSpec id ("" any)
    int difficulty = 3;                       // 1 easy .. 5 extreme
    double hours = 12;
};
struct Spec {
    std::string name = "REAL";    // shown as "REAL: NAME"
    std::string dir;              // the sidecar's folder (relative paths are from it)
    std::string source;           // the DEM file (as given)
    std::string sourcePath;       // ...and resolved
    std::string text;             // the sidecar as read (part of the hash)
    double metresPerPixel = 0;    // where the file doesn't say (PNG, plain TIFF), or to override it
    double verticalScale = 1;     // file units -> metres (a PNG's 0..65535 -> metres, say 0.1)
    double verticalOffset = 0;    // added after the scale (a PNG's sea level)
    double exaggeration = 1;      // heights over the lowest point are stretched by this
    double seaLevel = -1e9;       // water at or below this real elevation (m, after scale and offset); -1e9: none
    // The crop: pixels, or lat/lon bounds (south, west, north, east), or a centre (lat, lon) and the map's size.
    Window cropPx;
    bool hasBounds = false, hasCentre = false;
    double south = 0, west = 0, north = 0, east = 0, centreLat = 0, centreLon = 0;
    double size = 0;              // the map's side (m); 0: as much of the crop as fits, up to ~17 km
    double smooth = 1;            // passes of a light 3x3 smoothing on the DEM's pixels (0 none)
    double detail = -1;           // fine bumps added (m); -1: by the DEM's pixel size (30 m data more than 1 m lidar)
    int climate = CLIMATE_TEMPERATE;
    double snowLine = 1e9;        // real elevation (m) above which it's snow
    double treeLine = 1e9;        // and above which no trees grow
    double trees = 1, bushes = 1, rocks = 1;  // scatter densities (x the climate's own)
    double rockSize = 1;          // the scattered rocks' size (x the climate's own; the big scanned ones are cut into pieces, dear in memory)
    double ledgeStep = 0;         // ledges on the rock: the step's height (m; 0 none) ...
    double ledgeStrength = 0.8;   // ...how far toward a stair (0..1)
    double riverArea = -1;        // km2 of land draining to a point before it's a river with water (-1: the climate's)
    double washArea = -1;         // ...a dry wash (sand and gravel)
    double lakeArea = 0.004;      // km2: smaller closed hollows are left dry
    int outposts = -1;            // how many outposts (-1: by the map's area)
    uint32_t seed = 1;
    uint64_t hash = 0;            // the DEM file's bytes and the sidecar's text
    // Moab and the like (all optional):
    double rimWidth = 130;        // the rim that keeps you on the map: how far in from its edge it starts (m)
    double slickLo = 1e9, slickHi = -1e9;  // real elevations between which bare sandstone (slickrock) covers all but the flats and hollows
    std::string backdrop;         // a coarse DEM of the country round the map, for the horizon (as given) ...
    std::string backdropPath;     // ...and resolved
    double backdropScale = 1, backdropOffset = 0;  // its file units -> metres (`backdrop FILE [SCALE OFFSET]`: its own, not the DEM's)
    std::vector<SpecPlace> places;
    std::vector<SpecRiver> rivers;
    std::vector<SpecBridge> bridges;
    std::vector<SpecTrail> trails;
    std::vector<SpecCourse> courses;
    std::vector<SpecObstacle> obstacles;
    std::vector<SpecLandmark> landmarks;
    // `osm FILE`: OpenStreetMap ways and points the trails, obstacles and landmarks name by id (its text is part of the hash).
    std::string osm, osmPath, osmText;
    std::string includeText;      // (the `include` files' text: part of the hash)
    // `osm_rules 1`: besides the trails, obstacles and landmarks the sidecar names, the osm file's own by rules (realterrainplan.cpp,
    // docs/real-terrain.md): every 4x4 track (highway=track with 4wd_only=yes, or a named one) not already on a trail is a trail rated
    // by its tracktype and smoothness; every tourism=attraction on one is an obstacle (a pair of ledges) under its OSM name; the
    // trailheads and viewpoints are landmarks. What the sidecar authors by name or id wins. Any box's OSM data, no hand work.
    bool osmRules = false;
    std::vector<OsmWay> osmWays;
    std::vector<OsmNode> osmNodes;
    const OsmWay* osmWay(int64_t id) const;
    const OsmNode* osmNode(int64_t id) const;
};
bool parseSpec(const std::string& text, const std::string& dir, Spec& out, std::string* err = nullptr);
// A sidecar file, or a folder holding terrain.txt (or one *.terrain.txt): parsed, its DEM found and hashed.
bool loadSpec(const std::string& path, Spec& out, std::string* err = nullptr);
uint64_t hashFile(const std::string& path, bool* ok = nullptr);

// ---- The plan --------------------------------------------------------------------------------------------------------------
struct PlanRiver {
    std::vector<V3> pts;        // downstream, on the coarse grid's cells (y: the land there)
    std::vector<float> area;    // km2 drained at each
    bool wet = true;            // false: a dry wash
    // A sidecar's big river (SpecRiver): its name, width and depth as given, its water at the DEM's own surface along it.
    std::string name;
    double halfWidth = 0, depth = 0;  // (0: by the area drained)
};
struct PlanTrail {
    std::vector<V3> pts;        // from `from` to `to`, a coarse cell apart
    int from = -1, to = -1;     // indices into places (to -1: onto another trail)
    bool road = true;           // a road (<= 12%) or a trail (<= 30%)
    double maxGrade = 0.12;
    // A sidecar's named trail (SpecTrail): its name and rating; its line goes through the points given (pts is a cell apart).
    std::string name;
    int rating = -1;
    bool bypass = false;        // an obstacle's way round (PlanObstacle::bypass): laid as its trail is, bare on the rock
    bool osm = false;           // its line is OpenStreetMap's (SpecTrail::osmWays)
};
struct PlanBridge {
    std::string name;
    V3 a, b;                    // its ends on the banks (y: the deck there)
    double halfWidth = 5;
};
// A sidecar's obstacle (SpecObstacle), worked out on its trail.
struct PlanObstacle {
    SpecObstacle spec;
    int trail = -1;             // index into the plan's trails
    V3 at;                      // where it is (the point given), on the trail's line (y: the land before it's laid)
    double along = 0;           // m along the trail's smoothed line (smoothLine(pts, 1)) to it
    double from = 0, to = 0;    // the stretch of that line it takes (m along)
    int bypass = -1;            // its way round: a trail of the plan's (-1 none)
};
// A sidecar's course (SpecCourse), worked out on its trail: where it starts and the gates along it.
struct PlanCourse {
    SpecCourse spec;
    int trail = -1;             // index into the plan's trails
    std::vector<V3> line;       // the trail's line, smoothed, a metre or two apart (y: the land there before it's laid)
    double length = 0, climb = 0, drop = 0;  // m along it; m up and down along it (the land before it's laid)
    // The obstacles on it with a way round (m along `line`): no gate is put on them, so the way round is a way through.
    std::vector<std::pair<double, double>> optional;
    // The line with each of those taken by its way round (for a check that it can be driven; a metre or two apart).
    std::vector<V3> easiest;
};
struct PlanLake {
    double level = 0;           // its water (map height)
    double x = 0, z = 0;        // a cell of it
    double area = 0;            // m2
};
// The land prepared: the DEM cropped and in metres, and where the map is on it.
struct Terrain {
    Spec spec;
    Dem dem;              // the crop, in metres over `base` (exaggerated), smoothed
    Window window;        // which pixels of the file it is (empty: all of them)
    double half = 600;    // the map's half-size (whole metres)
    double base = 0;      // the real elevation of the map's y = 0 (the crop's lowest point)
    double mppX = 30, mppY = 30;  // metres per DEM pixel east and south
    double cx = 0, cy = 0;        // the DEM pixel (centre convention) at the map's middle
    double detailAmp = 0.3;
    // The land at (x, z) before anything is laid on it: bicubic through the DEM (demAt), fine detail added (landAt), and the
    // map's rim, which keeps you on it (heightAt). The plan reads demAt; the world is built from heightAt.
    double demAt(double x, double z) const;
    double landAt(double x, double z) const;
    double heightAt(double x, double z) const;
    double rimAt(double x, double z) const;
    double realElevation(double y) const { return base + y / (spec.exaggeration > 0 ? spec.exaggeration : 1); }
    // Where a latitude and longitude is on the map (x east, z north, m from the middle); false: the file isn't in degrees.
    bool toMap(const LatLon& ll, double& x, double& z) const;
    // The backdrop (Spec::backdrop): the country round the map out to its edge, coarse, in metres like `dem` (over `base`). Its
    // land at (x, z) on the map's frame (false: no backdrop, or off it).
    Dem backdrop;
    double bdCx = 0, bdCy = 0, bdMppX = 0, bdMppY = 0;  // the map's middle on it (pixel, centre convention) and its pixels' size
    double bdReach = 0;         // how far it reaches from the middle, the nearer way (m)
    bool backdropAt(double x, double z, double& y) const;
    double mapHeight(double real) const { return (real - base) * spec.exaggeration; }
    // The plan (made once, the first time it's asked for).
    struct Plan {
        double half = 0;     // (the map's)
        int n = 0;           // coarse cells a side
        double cs = 8;       // their spacing (m)
        std::vector<float> h;           // the land at each
        std::vector<int> lake;          // which lake a cell is under (-1 none)
        std::vector<float> acc;         // km2 drained through it
        std::vector<PlanLake> lakes;
        std::vector<PlanRiver> rivers;
        std::vector<PlanTrail> trails;
        std::vector<MapPlace> places;
        std::vector<double> padY;       // each place's pad height (its ground, levelled)
        std::vector<uint8_t> wet, wash; // the coarse cells a river's water, or a dry wash, runs through
        std::vector<PlanBridge> bridges;
        std::vector<PlanCourse> courses;
        std::vector<PlanObstacle> obstacles;
        struct Landmark {
            std::string name;
            V3 at;
        };
        std::vector<Landmark> landmarks;
        double cx(int i) const;
        int cellOf(double v) const;
    };
    const Plan& plan() const;
    mutable std::once_flag planOnce_;
    mutable std::unique_ptr<Plan> plan_;
};
// A coarse line made smooth (corner cutting, its ends kept) and resampled every `step` m: how the plan's rivers and trails are laid.
std::vector<V3> smoothLine(const std::vector<V3>& pts, double step);
// Reads and prepares a spec's DEM (false: no file, a bad file, or a crop off it).
std::shared_ptr<const Terrain> prepare(const Spec& spec, std::string* err = nullptr);

// ---- The real map the game plays as MAP_REAL ------------------------------------------------------------------------------
// The sidecars found in maps/ (maps/<name>/terrain.txt or maps/<name>/<name>.terrain.txt; also data/real/<name>/), by name.
std::vector<std::string> discover();
// Which one MAP_REAL is (nullptr: none; World(MAP_REAL) is then a plain field). Thread-safe; a terrain set stays alive.
std::shared_ptr<const Terrain> active();
void setActive(std::shared_ptr<const Terrain> t);
bool selectPath(const std::string& path, std::string* err = nullptr);  // load and make it active
bool selectByHash(uint64_t hash, const std::string& nameHint = "");   // among discover()'s (false: none has it)
bool available();          // one is active, or discover() found one (then the first is made active)
uint64_t activeHash();     // 0: none
std::string activeName();  // "MOAB" ("" none)
double activeHalf();       // its half-size (600 with none)
const char* displayName(); // "REAL: MOAB" (stable pointer)
const std::vector<MapPlace>& places();  // the active one's (empty with none)
// The active one's courses (empty with none): what the level list makes its sprints from (level.cpp realCourseLevels).
const std::vector<PlanCourse>& courses();
// Tests: on this thread, this terrain is the active one (and discover() looks only in `roots`), whatever the game's is.
struct ScopedTerrain {
    explicit ScopedTerrain(std::shared_ptr<const Terrain> t, std::vector<std::string> roots = {});
    ~ScopedTerrain();
    ScopedTerrain(const ScopedTerrain&) = delete;
    ScopedTerrain& operator=(const ScopedTerrain&) = delete;
private:
    std::shared_ptr<const Terrain> prev_;
    bool prevSet_;
    std::vector<std::string> prevRoots_;
};

}  // namespace realterrain
