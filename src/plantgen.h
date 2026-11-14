// The plant generator: trees, shrubs, grasses, yuccas and cacti grown by rules from a species description, so a new species is a
// text file (assets/plants/NAME.txt) and not a hand-written model. docs/procedural-plants.md explains the keys and how to add one.
//
// A species says how the plant grows (its form and branching rules, its foliage, its colours); generate() grows one individual
// from a seed: a skeleton of stems and branches (recursive growth, thicknesses by the pipe model), tubes round it, and foliage as
// small cards whose normals follow the clump and the crown, so a crown shades as a volume. The same seed gives the same mesh on
// every machine: only + - * / sqrt and detmath.h, its own integer random numbers, no unordered containers.
//
// Trees are built 10 tall (forest.h modelHeight) with the origin at the trunk's foot; shrubs 0.8 in half-width (the bush model's
// size: forest::buildBushes). `lod` 0..3: the same plant from fewer triangles (forest.h kLods).
//
// No OpenGL and nothing of the world here: forest.cpp draws them, src/models/procplants.cpp shows them in the model kit (the model
// lab, the proving ground's yard, build/modelsheet). A rock or a building generator would sit beside this one the same way: its
// own data folder, its own generate(), a late-registered model for the kit.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "modelkit.h"

namespace plantgen {

constexpr double kTreeHeight = 10.0;   // a generated tree's height (model units)
constexpr double kShrubRadius = 0.8;   // a generated shrub's half-width

struct Species {
    std::string key;     // its file's name: what a vegetation palette calls it
    std::string title;   // `name`
    std::string blurb;   // `about`
    std::string form;    // excurrent | decurrent | multistem | shrub | wand (all grown as a branching skeleton), bunchgrass, rosette, pads
    bool tree = false;   // `class tree`: stands in World::trees with a trunk to hit; else a shrub (World::bushes, nothing to hit)
    int id = -1;         // `id`: a number that never changes (World::Tree::design of kind 4; World::Bush::kind - kShrubKindBase)
    std::vector<std::string> replaces;  // `replaces pine 0`, `replaces bush sagebrush`: the classic model it stands in for
    std::map<std::string, std::vector<double>> num;
    std::map<std::string, std::string> word;
    // A key's i-th value (past its last: the last; not set: the built-in default).
    double v(const char* k, size_t i = 0) const;
    V3 col(const char* k) const;
    bool has(const char* k) const { return num.count(k) > 0; }
    std::string w(const char* k) const;
    bool woody() const { return form != "bunchgrass" && form != "rosette" && form != "pads"; }
};

struct Options {
    uint32_t seed = 1;
    int lod = 0;
    bool snow = false;
    double dry = 0;      // 0 green .. 1 its dry colours (a shrub in drought)
    int season = 0;      // 0 summer, 1 late summer, 2 autumn (toward its `autumn` colour)
    double breast = 0;   // > 0: the trunk's radius 1 unit up (model units) made this (a classic tree's, whose collider it keeps)
};

// Grows one. The mesh is appended to and finished.
void generate(const Species& s, const Options& o, mk::Mesh& out);
// The main stem's radius at a height (model units), seed 1: what its collider is made from.
double trunkRadius(const Species& s, double y);

// A species from its file's text (`key`: the file's name). False with `err` for an unknown key or a missing value.
bool parse(const std::string& key, const std::string& text, Species& out, std::string* err = nullptr);

// Every species in assets/plants (found like the other assets; $CZ_PLANTS_DIR: another folder), by name. Loaded once.
const std::vector<Species>& all();
const std::string& dir();
const std::vector<std::string>& loadErrors();  // files that didn't parse (the tests fail on any)
const Species* find(const std::string& key);
const Species* treeById(int id);
const Species* shrubById(int id);
// The species that stands in for a classic model ("pine 0", "broadleaf 1", "bush sagebrush"): null none.
const Species* replacing(const std::string& what);

// The keys a species file may set, with their defaults and what they mean (docs, the tests).
struct KeyInfo {
    const char* key;
    const char* def;
    const char* what;
};
const std::vector<KeyInfo>& keys();

// Every species file's text, hashed: part of a map's hash where a palette grows them (a trunk's Pole comes from its species).
uint64_t textHash();

// Classic maps drawn with the generated plants, each biome its own (forest.cpp, the palettes' `stand` lines). On by default;
// --classic-plants or CZ_CLASSIC_PLANTS=1 restores the classic models exactly. A look only: the world is the same either way.
bool everywhere();
void setEverywhere(bool on);
// For a test: on this thread only, whatever the switch says (scenarios run side by side).
struct EverywhereHere {
    explicit EverywhereHere(bool on);
    ~EverywhereHere();
    int was;
};

// World::Bush::kind for a generated shrub with no plant kind of its own (plants.h): kShrubKindBase + its id.
constexpr int kShrubKindBase = 1000;

}  // namespace plantgen
