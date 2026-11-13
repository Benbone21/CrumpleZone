// Vegetation palettes: what grows where on a real-terrain map (realterrain.h), as data. A sidecar's `vegetation NAME` names a
// palette file (assets/maps/vegetation/NAME.txt, or NAME.txt beside the sidecar); the build sorts every spot into a zone by its
// ground cover, slope, real elevation and distance to water, and the palette says how many trees and shrubs a hectare of that zone
// carries and in what mix of species. No map is named in the code: another place is another file.
//
// A palette file (one thing a line, # comments):
//   name COLORADO PLATEAU DESERT
//   upland FROM FULL              real elevations (m): lowland below FROM, upland from FULL up, mixed between
//   riparian REACH RISE           within REACH m of water and no more than RISE m above it: the riparian zone
//   wash REACH RISE               the same along a dry wash
//   slope TREES SHRUBS            the steepest ground (rise over run) each grows on
//   open SHARE                    where the map has no ground cover from photos: the share of the ground that carries plants
//   sand_water SHARE              the riparian and wash densities on bare sand (bars and banks; cover says sand)
//   crown CELLS                   trees only where this many of the 5 cover samples round the spot are brush (a crown-sized patch)
//   zone NAME TREES SHRUBS        rock | sand | lowland | upland | riparian | wash, a hectare of ground that carries plants
//     tree SPECIES WEIGHT HMIN HMAX [crown R] [above E] [below E]   (the zone above's; heights in m; R: its crown's radius
//                                                                   over its height; E: real elevations it keeps between)
//     shrub SPECIES WEIGHT [size S] [above E] [below E]             (S: x the kind's own size range)
// Species are the code-made models' names (treeSpecies(), shrubSpecies()): trees pine, broadleaf, snag, grey_snag, juniper, pinyon, cottonwood, tamarisk;
// shrubs the plant kinds (plants.h) in lower case with underscores (sagebrush, blackbrush, mormon_tea, prickly_pear, ...).
#pragma once
#include <string>
#include <vector>

namespace veg {

enum Zone { Z_ROCK, Z_SAND, Z_LOWLAND, Z_UPLAND, Z_RIPARIAN, Z_WASH, Z_COUNT };
const char* zoneName(int zone);

struct TreeSpec {
    std::string species;
    int kind = 0, design = -1;   // World::Tree::kind and design (-1: the kind's own mix, forest.h designFor)
    double weight = 1, hMin = 3, hMax = 6;
    double crown = 0.3;          // the crown's radius over the height (how far it keeps off a trail, what the camera fades)
    double above = -1e9, below = 1e9;  // real elevations (m) it grows between
};
struct ShrubSpec {
    std::string species;
    int kind = 0;                // PlantKind
    double weight = 1, size = 1;
    double above = -1e9, below = 1e9;
};
struct ZoneRule {
    double treesPerHa = 0, shrubsPerHa = 0;
    std::vector<TreeSpec> trees;
    std::vector<ShrubSpec> shrubs;
    // The species at `u` in [0, 1) of the mix of those that grow at `elevation` (null: none).
    const TreeSpec* tree(double u, double elevation) const;
    const ShrubSpec* shrub(double u, double elevation) const;
};
struct Palette {
    std::string name;
    double uplandFrom = 1e9, uplandFull = 1e9;
    double riparianReach = 60, riparianRise = 5;
    double washReach = 20, washRise = 3;
    double treeSlope = 0.55, shrubSlope = 0.75;
    double open = 0.12;
    double sandWater = 0.25;
    int crown = 4;
    ZoneRule zone[Z_COUNT];
    double maxTrees() const;   // the densest zone's (a hectare)
    double maxShrubs() const;
};

// What the build knows of a spot.
struct Site {
    int cover = 0;          // the photos' class: 0 unknown, 1 bare rock, 2 sand, 3 brush
    bool rock = false;      // the ground is bare rock (its surface), or snow or ice
    bool sand = false;      // ... sand
    double elevation = 0;   // real, m
    double water = 1e9, waterRise = 1e9;  // to the nearest water (m), and how far above it the spot stands
    double wash = 1e9, washRise = 1e9;    // the same for a dry wash
};
// Its zone; `u` in [0, 1) settles the band between lowland and upland. `density`: the share of the zone's plants this spot
// carries (bare sand by the water is thinner).
int zoneOf(const Palette& p, const Site& s, double u, double* density = nullptr);

bool parse(const std::string& text, Palette& out, std::string* err = nullptr);
// The file for `name` (NAME.txt beside the sidecar in `dir`, else in the maps' shared vegetation folder beside `dir`): "" none.
std::string findFile(const std::string& name, const std::string& dir);
std::vector<std::string> treeSpecies();
std::vector<std::string> shrubSpecies();

}  // namespace veg
