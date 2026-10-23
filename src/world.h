#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "mathx.h"
#include "plants.h"

// Oriented static box (ramps, walls, poles, bumps).
struct Box {
    V3 c, h;      // center, half extents
    V3 ax[3];     // local axes in world space
    float col[3];
    double bound; // bounding-sphere radius
    bool hidden = false;  // collision only (drawn some other way)
    bool solid = true;    // false = drawn only (the collider is something else, e.g. a pole)
    bool rock = false;    // a rock slab: drawn as rock, grips like rock
};

// A rock's orientation when it isn't left to chance (World::addRock): its long axis (the first half-size) runs along
// `yaw` (radians, the game's: 0 = +X, pi/2 = -Z) climbing `pitch`, and the rock is rolled `roll` about that axis (right-handed).
struct RockPose {
    double yaw = 0, pitch = 0, roll = 0;
    int scan = -1;  // with scanned rocks on: exactly this one of the library (its own proportions; the half-sizes just scale it), not whichever fits
};

// Boulder: an irregular convex rock (the hull of points scattered over a squashed,
// tilted ellipsoid), collided with exactly as it's drawn.
struct Rock {
    V3 c;           // bounding-sphere center
    double r = 0;   // bounding radius
    float tint = 1;
    uint32_t seed = 0;
    std::vector<V3> v;                  // corners (world)
    std::vector<std::array<int, 3>> f;  // faces, wound outward
    std::vector<V3> fn;                 // face normals
    std::vector<double> fd;             // plane offsets: dot(fn, p) == fd on the face
    // A scanned rock (props::activeRockScans() was on when it was added): its hull in the
    // library, and the scan's model frame in the world (world = origin + axis * model, the
    // axes scaled), which the renderer draws the scan's mesh with. -1: a code-made rock.
    int scan = -1;
    V3 axis[3], origin;
    // A big scanned rock the single hull above overstates (a convex hull bridges a lumpy scan's hollows, tens of cm once scaled up):
    // the convex pieces it is really made of (each with its own c, r, v, f, fn, fd; none with parts). What is hit and driven on is
    // these; the hull above stays as the whole's bound and its look-alike for everything else. Empty: the hull is the rock.
    std::vector<Rock> parts;
    bool partsDue = false;  // (placed while its world was being made: the world cuts it at the end, World::splitPendingRocks)
    V3 at;  // the point its pose turns it about (code-made: the middle of its points; scanned: the scan's middle)
};

// Fallen log: a capsule from a to b.
struct Log {
    V3 a, b;
    double r;
};

// A lake: flat water at height y within r of (x, z); the shore is wherever the
// basin carved under it rises above y.
struct Water {
    double x, z, r, y;
};

// Man-made and odd bits of scenery, drawn by the renderer. Whatever is solid about
// them is added separately as boxes, poles or logs.
enum PropKind { PROP_FENCE, PROP_CABIN, PROP_SIGN, PROP_MAPBOARD, PROP_TABLE, PROP_STUMP, PROP_GARAGE, PROP_MARKER, PROP_CAIRN };
struct Prop {
    PropKind kind;
    V3 a, b;          // position (fences: from a to b)
    double yaw = 0;   // facing (radians, 0 = +X)
    float col[3] = {0, 0, 0};  // signs: the difficulty color
};

// A code-made model (modelkit.h, src/models/) placed on the map: drawn only; whatever is solid
// about it is added separately as boxes or poles. Drawn as picked in the model lab.
struct Placed {
    std::string id;
    V3 p;                    // its origin, on the ground
    double yaw = 0;          // facing (radians, 0 = +X)
    V3 scale{1, 1, 1};       // (front, up, left)
    std::vector<std::pair<std::string, double>> set;  // its own params: a sign's rating, a crate opened
    int variant = -1;        // (the model lab: this design, not the picked one)
    bool oriented = false;   // turned any way at all (things in your hands): its front, up and left
    V3 ax[3];
    bool bare = false;       // a part fitted in a car (carparts.h): built without the stand it's shown on (cached)
    bool sunlit = false;     // lit as if out in the sun, whatever shades it (a truck's load, under its roof: trunk.h)
    // A damaged part (carparts.h): 1 bent, 2 broken (the lit shader tints it and rims it hot), 3 dead (sooty).
    uint8_t wear = 0;
    // Only this stretch of the model along its X is drawn (a snapped shaft's halves: the cut end is capped by another model).
    bool clip = false;
    float clipLo = -1e9f, clipHi = 1e9f;
};

// A spawn pad (spawnpad.h): the slab vehicles start on. A rectangle in its own frame: u ahead of p (the
// way you face) from -back to +front, v to the left +-half, then a ramp `ramp` m long down to the ground
// ahead (`drop` m lower at its end).
struct SpawnPad {
    V3 p;                      // the point vehicles start on (slot 0), on the slab
    double yaw = 0;
    double back = 0, front = 0, half = 0, ramp = 0, drop = 0;
    int slots = 1, cols = 1;
    double openAhead = 0;      // m of lane past the front kept clear of trees and rocks (a map's start: so it leads somewhere)
    bool drawn = true;         // false: it was a concrete paddock already (nothing was changed or drawn)
};

// A repair station: a concrete pad under an open shed. Park on it, stop, and the crew
// fixes whatever is broken.
struct Station {
    V3 p;              // pad center (on the ground)
    double yaw = 0;    // the shed's open side faces this way
    const char* name = "";
    double r = 7.5;    // how far from the center counts as on the pad
};

// Scenery only (no collision).
struct Bush {
    V3 p;
    double r;
    float tint;
    int kind = 0;  // PlantKind (plants.h): 0 the round leafy shrub, then sage, juniper, ferns, reeds, cacti...
};

// A bush of the kind that grows in `habitat` (plants.h): `u` in [0, 1) sizes it within its kind's range, `edge`: at the edge of a wood.
// The kind comes from a hash of the spot, not from the map's random numbers (so a scatter's layout doesn't depend on the kinds).
Bush growBush(const V3& p, int habitat, bool edge, double u, float tint);

// Vertical cylinder obstacle: poles and tree trunks. Cars test both their nodes and
// their panel edges against these, so thin obstacles can't slip between nodes.
struct Pole {
    double x, z, y0, y1, r;
};

struct Tree {
    V3 base;
    double height, radius;
    float tint;
    int kind = 0;  // 0 pine, 1 broadleaf, 2 dead snag
    int design = -1;  // its model's variant (forest.h designFor; -1: worked out when drawn)
};

enum Surface : uint8_t {
    SURF_PAVED, SURF_GRASS, SURF_DIRT, SURF_ROCK, SURF_SCREE,
    SURF_GRAVEL, SURF_SAND, SURF_MUD, SURF_SNOW, SURF_ICE, SURF_WET,
    SURF_COUNT
};
// How a surface treats a tire.
//   grip     peak friction, as a share of dry pavement
//   bite     how quickly grip builds with slip: loose ground shears and gives (a soft,
//            progressive slide), pavement and ice bite at a few degrees
//   keep     grip left when sliding well past the peak: pavement lets go (~70%), loose
//            stuff piles up in front of the tire and keeps pushing back (~95-100%)
//   rolling  rolling resistance (share of the load): sand and mud drag you down
//   sink     how deep a tire can sink into it (m): sand, mud and snow give under the
//            load, deeper with high pressure and spinning wheels, less at speed
//   dust     color of what the tires throw up (r, g, b)
struct SurfaceInfo {
    const char* name;
    double grip, bite, keep, rolling, sink;
    float dust[3];
};
const SurfaceInfo& surfaceInfo(Surface s);
// Tire grip on each surface, as a share of dry pavement.
inline double surfaceGrip(Surface s) { return surfaceInfo(s).grip; }

// MAP_WILDS is the clean ground that obstacle-section levels are set in: natural country with nothing laid out on it (no
// course, pad, yard or park), dressed as one of the WildsKinds (World::wildsKind when it's built). Not on the map list: only levels use it.
enum MapId { MAP_PROVING_GROUND, MAP_MOUNTAIN, MAP_FREEWAY, MAP_FRONTIER, MAP_BACKCOUNTRY, MAP_WILDS, MAP_COUNT };
// What the wilds look like: which ground and growth surround the obstacle (the section's biome, biome.h).
enum WildsKind { WILDS_MEADOW, WILDS_DESERT, WILDS_FOREST, WILDS_CANYON, WILDS_WETLAND, WILDS_UPLAND, WILDS_COUNT };
const char* wildsKindName(int kind);
const char* mapName(int map);  // as the menu shows it
// The map whose music and ambience a map uses (the wilds sound like the proving ground's open country).
inline int soundMap(int map) { return map == MAP_WILDS ? (int)MAP_PROVING_GROUND : map; }
// The maps on offer (menus, hosting, the editor): just the proving ground, where everything in
// the game is laid out to look at and try, while the world is rebuilt. (The others are still
// built here, for the tests and --map.)
const std::vector<int>& listedMaps();
bool mapListed(int map);
inline bool mapForLevels(int map) { return map == MAP_WILDS || mapListed(map); }  // (levels can be on a listed map or on the wilds)
int nextListedMap(int map, int dir);  // the next one on the list (from any map)

// Catmull-Rom spline through control points, sampled every ~2 m.
std::vector<V3> splinePoints(const std::vector<V3>& pts, bool closed);
// Where a named feature of the mountain map is (crawl lanes start at their south end):
// twister, boulders, ledges, logs, notch, dome, trailhead, summit.
V3 mountainFeature(const char* name);
std::vector<V3> mountainTrail();  // the summit trail's center line, trailhead to lookout
// The Frontier's features (start, mesa, mesaclimb, wall, garden, summit, tarn, lake, bog,
// delta, bowl, ford1-4, track, outpost, lakeside, hut), and its race circuit's center line.
V3 frontierFeature(const char* name);
const std::vector<V3>& frontierTrack();
// The Backcountry's places and trails (see backcountry.cpp): trailhead, creekcrawl, creekend, mine,
// slab, ridgeend, deepcrossing, ford, farside, sawmill, hillclimb, hilltop, dunes, wash, mudstart,
// bog1-4, shelf, shelfend, wreck, pass, passhut, firetower, cabin, ...
V3 backcountryFeature(const char* name);
// A trail's center line, ~1 m apart, in the direction of travel: "creek", "ridge", "mud", "wash",
// "hill", "ridgeroad", "shelf", "pass", "tower", "river", "cabin".
const std::vector<V3>& backcountryTrail(const char* name);

struct Contact {
    V3 n;            // surface normal, pointing out of the obstacle
    double pen = 0;  // penetration depth (> 0 when touching)
    Surface surface = SURF_PAVED;
};

// What the ground looks like at a point, for the terrain shader.
enum Paint : uint8_t { PAINT_NONE, PAINT_ROAD, PAINT_TRAIL, PAINT_ROCK, PAINT_GRAVEL, PAINT_PAD, PAINT_SCREE,
                      PAINT_SAND, PAINT_MUD, PAINT_SNOW, PAINT_ICE, PAINT_WET, PAINT_FREEWAY,
                      PAINT_TRACK, PAINT_DESERT, PAINT_REDROCK };
struct RoadInfo {
    bool near = false;  // within a few meters of a road (or painted: trail, rock, gravel)
    uint8_t paint = PAINT_NONE;
    float lateral = 0;  // signed distance from the center line (m)
    float along = 0;    // distance along the road (m), for dashed lines
};

class World {
public:
    static constexpr double kHalfSize = 600.0;
    static constexpr double kPadRadius = 140.0;
    // The proving ground's test pad: a flat, paved, gridded rectangle (x0, x1, z0, z1) holding the course, the ramps and the model yard;
    // the land rises to the hills over kZoneRamp m beyond its edge. padOutside: how far (x, z) is outside it (negative inside).
    static constexpr double kZoneX0 = -230, kZoneX1 = 150, kZoneZ0 = -185, kZoneZ1 = 140, kZoneRamp = 60;
    static double padOutside(double x, double z);
    static constexpr double kRoadHalfWidth = 5.0;

    std::vector<Box> boxes;
    std::vector<V3> loopRoad;       // center line of the loop road, ~2 m spacing (closed)
    std::vector<double> loopAlong;  // distance along the loop at each sample
    double loopLength = 0;
    std::vector<Tree> trees;
    std::vector<Pole> poles;
    std::vector<Rock> rocks;
    std::vector<Log> logs;
    std::vector<Bush> bushes;
    std::vector<Water> waters;
    std::vector<Prop> fixtures;  // fences, cabins, signs, tables, stumps
    std::vector<Placed> models;  // code-made models (modelkit.h)
    // Names shown over things when you're near (the proving ground's model yard).
    struct Tag {
        std::string text;
        V3 p;
    };
    std::vector<Tag> tags;
    // Whether the proving ground gets its showcase, the model yard and the section park (everything
    // the game makes, laid out to look at and try): the game turns it on; the tests keep the pad as
    // it always was. (Per thread: the tests build both kinds side by side.)
    static inline thread_local bool withShowcase = false;
    // Whether the park's sections are built from the player's own saved edits of those levels (sectionfile.h: editedSectionFor), else all
    // from the generator. Off in multiplayer: the others build the proving ground from their own files, so the edits would desync it.
    static inline std::atomic<bool> useLocalEdits{true};
    bool parkEdited = false;      // some park section came from a saved edit
    uint64_t editStamp = 0;       // levelFilesStamp() when the park was built
    // The WildsKind the next MAP_WILDS is built as (a level's `terrain`: see levelWorld in level.h). Per thread, like withShowcase.
    static inline thread_local int wildsKind = WILDS_MEADOW;
    // The park's obstacle sections (sections.h), in the order they're driven: where each starts.
    struct SectionSpot {
        std::string name;
        int type = 0;         // SectionType
        V3 entry, exit;       // on the ground
        double heading = 0;   // radians, the game's yaw
        V3 spawn;             // where a vehicle starts to drive it: on its spawn pad, a little before the entry
        double spawnYaw = 0;
    };
    std::vector<SectionSpot> sections;
    std::vector<Station> stations;
    // Water surface height at (x, z), or -1e9 where there's none.
    double waterLevel(double x, double z) const {
        if (!waterY.empty()) {
            const int i = (int)std::lround((x + kHalfSize) / kRes), j = (int)std::lround((z + kHalfSize) / kRes);
            if (i < 0 || j < 0 || i >= hN || j >= hN) return -1e9;
            return waterY[i * hN + j];
        }
        for (const Water& wt : waters)
            if ((x - wt.x) * (x - wt.x) + (z - wt.z) * (z - wt.z) < wt.r * wt.r) return wt.y;
        return -1e9;
    }
    // Which way the water there flows, and how fast (m/s): rivers push you downstream.
    V3 waterFlow(double x, double z) const {
        if (flowX.empty()) return V3();
        const int i = (int)std::lround((x + kHalfSize) / kRes), j = (int)std::lround((z + kHalfSize) / kRes);
        if (i < 0 || j < 0 || i >= hN || j >= hN) return V3();
        return V3(flowX[i * hN + j], 0, flowZ[i * hN + j]);
    }
    bool hasWater() const { return !waters.empty() || !waterY.empty(); }
    // Maps with rivers keep their water on the height grid: level (-1e9 dry) and flow.
    std::vector<float> waterY, flowX, flowZ;
    int gridSize() const { return hN; }
    double frontierHeight(double x, double z) const;  // the Frontier's land before anything's carved into it

    int map = MAP_PROVING_GROUND;
    V3 spawn;                    // where the player starts (on the ground)
    double spawnYaw = 0;
    std::vector<SpawnPad> spawnPads;  // the slabs vehicles start on (spawnpad.h): the map's start, its parking, a level's start
    // Slot k of the pad at the map's start (or the level's): where the kth vehicle starts and which way it faces. False
    // when there's no such pad or it has no kth slot.
    bool spawnSlot(int k, V3& p, double& yaw) const;
    std::vector<V3> parking;     // where the other vehicles start, facing parkingYaw
    double parkingYaw = M_PI / 2;
    bool crashProps = true;      // crash boxes and barriers (the proving ground)
    // The freeway repeats every wrapPeriod meters along x (0: the map doesn't). The sim
    // shifts everything back by a period as you drive on, so it never ends.
    double wrapPeriod = 0;
    // How thick the grass grows at (x, z), 0..1 (the renderer's grass blades).
    float grassDensity(double x, double z) const;
    // Adds a boulder at (x, z): half-sizes (along, up, across), `top` of it showing above
    // the ground, turned to a random yaw and tipped a little. Returns it. With scanned hulls
    // on (props::activeRockScans()) it's the scan whose proportions fit best, scaled to the
    // half-sizes; else a code-made hull.
    // With `pose` it isn't turned or tipped at random but as asked (a ledge's row of rocks, a ramp): the first half-size
    // lies along the pose's yaw, and `top` is then its highest point's height over the ground at (x, z).
    Rock& addRock(double x, double z, const V3& radii, double top, uint32_t seed, float tint = 1, const RockPose* pose = nullptr);
    // Indices of poles whose cylinders may overlap the box [mn, mx] (appended to out).
    void polesNear(const V3& mn, const V3& mx, std::vector<int>& out) const;
    // Every solid whose lookup cells touch the rectangle [x0, x1] x [z0, z1]: indices into
    // poles, boxes, rocks and logs, each once. A superset (the lookup cells are coarse), found
    // without walking the whole map; what the autopilot's sensors cast their rays against.
    struct NearbySolids {
        std::vector<int> poles, boxes, rocks, logs;
        void clear() { poles.clear(), boxes.clear(), rocks.clear(), logs.clear(); }
    };
    void solidsNear(double x0, double z0, double x1, double z1, NearbySolids& out) const;

    explicit World(int map = MAP_PROVING_GROUND);
    // Big scanned rocks placed while the world is being made get their pieces (Rock::parts) all together at the end of it.
    bool deferRockSplits = false;
    void splitPendingRocks();
    double height(double x, double z) const;  // terrain only (baked grid, bilinear)
    V3 normal(double x, double z) const;
    RoadInfo roadAt(double x, double z) const;
    Surface surfaceAt(double x, double z) const;
    // The proving ground's surface course: which strip (x, z) is on (-1 none), and where
    // a strip starts (its west end, facing east).
    int courseSurface(double x, double z) const;
    V3 courseStart(int surface) const;
    // Deepest contact of a sphere of radius R (axis == nullptr), or of a wheel:
    // a disc of radius R and half-width hw spinning about `axis`.
    bool contact(const V3& c, double R, const V3* axis, double hw, Contact& out) const;
    // Lowest height at (x, z) where a 1 m sphere fits without touching anything.
    double clearY(double x, double z) const;

private:
    // Terrain baked to a 1 m grid with the roads carved in.
    static constexpr double kRes = 1.0;
    int hN = 0;
    std::vector<float> hgrid, roadLat, roadAlong;
    std::vector<uint8_t> roadNear;
    double baseHeight(double x, double z) const;  // analytic hills before roads
    void bakeTerrain();
    double gridAt(int i, int j) const { return hgrid[i * hN + j]; }
    std::vector<uint8_t> surf, paint;  // baked surface and paint (mountain); empty = worked out
    void buildProvingGround();
    void buildFreeway();
    void buildFrontier();
    void buildBackcountry();
    void buildWilds();       // the clean open country for obstacle sections (world.cpp)
    void buildModelYard();   // every model laid out on the proving ground (world.cpp)
    void buildLapLine();     // the lap timer's start/finish gate on the loop road (world.cpp)
    void buildSectionPark(); // the obstacle sections laid out outside the loop road (world.cpp)
    void buildMountain();
    double mountainHeight(double x, double z) const;
    void scatterScenery(uint32_t seed);  // bushes and rocks between the trees
    // Digs a lake basin `depth` deep at (x, z) radius r, banks rising at ~1:4, and fills it.
    void addLake(double x, double z, double r, double depth);

    static constexpr double kRockCell = 4.0;
    int rockN = 0;
    std::vector<std::vector<int>> rockGrid, logGrid;
    int rockCellOf(double v) const;

    static constexpr double kCell = 16.0;
    int gridN = 0;
    std::vector<std::vector<int>> grid, poleGrid;

    int cellOf(double v) const;

public:
    void addRamp(V3 from, V3 to, double width, double thick, V3 col);
    // For tests and anything that adds obstacles after the map is built: add, then
    // rebuild the lookup grids.
    void addBox(V3 c, V3 h, double yaw, double pitch, double roll, V3 col);
    void buildGrid();
    uint64_t stamp = 0;  // changes with every buildGrid(): anything cached about the map is stale
    // A repair station on flat ground at p: the pad, the open drive-through shed (its
    // corner posts are solid) and its sign.
    void addStation(V3 p, double yaw, const char* name);
    // Puts a code-made model (modelkit.h) here, if there is one by that id: whether there was.
    // (Map builders use it to draw a model over the plain boxes that stood in for it, keeping
    // the boxes as its collider.)
    bool placeModel(const std::string& id, V3 p, double yaw, std::vector<std::pair<std::string, double>> set = {}, V3 scale = V3(1, 1, 1));
    // ...and hides boxes [from, end) behind it if it was placed.
    bool standIn(size_t fromBox, const std::string& id, V3 p, double yaw, std::vector<std::pair<std::string, double>> set = {});

    // ---- Editing: level files and the world editor ------------------------------------
    // The terrain is a 1 m grid of cells over the whole map (gridSize() a side); each has
    // a height, a surface (how it grips) and a paint (how it looks).
    int cellIndex(double x, double z) const;  // the cell under (x, z), -1 off the map
    V3 cellCenter(int id) const;              // (x, its height, z)
    float cellHeight(int id) const { return hgrid[id]; }
    void setCellHeight(int id, float h) { hgrid[id] = h, cellEdits++; }
    uint64_t cellEdits = 0;  // counts the cells changed by hand (heights, surfaces, water): what's cached about how they look is stale
    uint8_t cellSurface(int id) const;
    uint8_t cellPaint(int id) const;
    void setCellSurface(int id, uint8_t surface, uint8_t paintLook);
    void setCellWater(int id, float level);   // -1e9: dry
    // A tree (with its trunk to hit) and a log, as the maps add them.
    void addTree(double x, double z, double height, int kind, float tint = 1);
    void plant(Tree t);  // a tree made up elsewhere (base, height, radius, tint, kind): its design settled, its trunk's Pole stood up
    void addLog(V3 a, V3 b, double r);
    // Removes whatever object is nearest (x, z) within r: rocks, trees, logs, boxes,
    // stations. Returns what it removed ("" nothing).
    // onlyKind: only that kind of thing counts (0 rock, 1 tree, 2 log, 3 box, 4 fixture, 6 bush) and distance is to its centre; -1 any.
    std::string removeNear(double x, double z, double r, int onlyKind = -1);
};
