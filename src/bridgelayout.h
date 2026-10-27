// A timber bridge's deck, rails and posts as worn by the years: which plank pieces are there (and which of those sag or are broken
// and tipped), which posts lean or are gone, which rails are snapped or gone. One pure function of the bridge's numbers, used twice:
// by the model (src/models/bridges.cpp: what's drawn) and by procdress.cpp (what's driven on: a box a plank piece), so the two agree.
//
// The bridge's own frame (the model's): x along the span, z to its left, y up, the deck's top at y = 0. The deck reaches E = L/2 + 1
// past the middle each way (onto the abutments), across +-hw.
//
// Every bridge keeps one drivable line: two wheel tracks (lineZ +- 0.5..1.05 m: a truck's wheels with some room either way) along which no hole is longer than maxGap.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace proc {

struct BridgeLook {
    double length = 30, hw = 2.6;  // span (m) and the deck's half-width
    double damage = 0.7;           // 0 sound .. 1 the worst (under kFairDamage: a bridge in fair shape, its deck whole)
    double missing = 0.35;         // the share of plank pieces gone at damage 1
    double maxGap = 0.75;          // m: the longest hole along the drivable line
    uint32_t seed = 1;
};
constexpr double kFairDamage = 0.25;
constexpr double kPlankPitch = 0.32, kPlankGap = 0.035, kPlankThick = 0.1;
constexpr double kTrackIn = 0.5, kTrackOut = 1.05;  // the drivable line's wheel tracks: this far either side of its middle

struct BridgeLayout {
    // A plank piece: [x0, x1] along, [z0, z1] across, its top `drop` below the deck's (sagging), rolled `roll` radians about its own
    // along-axis (broken: one end down on the stringer). kind 0 sound, 1 sagging, 2 broken.
    struct Piece {
        double x0, x1, z0, z1, drop, roll;
        uint8_t kind;
        double topAt(double z) const;  // its top's height at z (within it)
    };
    std::vector<Piece> deck;           // (pieces gone are left out)
    // Posts: 5 a side at x = -L/2 + k L/4, z = +-(hw + 0.05). state 0 standing, 1 leaning `lean` radians outward (`tip` along), 2 gone.
    struct Post { double x, z, lean, tip; int state; };
    Post posts[2][5];
    int rail[2][4] = {};               // the rail between posts k and k+1: 0 whole, 1 snapped (hanging: not solid), 2 gone
    double lineZ = 0;                  // the drivable line's middle
    double E = 16, Lh = 15, hw = 2.6;
    bool fair = false;                 // a fair bridge: its deck whole (the stand-in's one box is its collision) with running planks on top
    double damage = 0;
};

BridgeLayout bridgeLayout(const BridgeLook& look);

// The longest stretch along a wheel track at z with no plank piece under it (the deck's ends included: it's on its abutments there).
double longestHole(const BridgeLayout& b, double z);
// ...the worst over the drivable line's tracks.
double lineWorstHole(const BridgeLayout& b);

// The parts under the deck, where the model draws them and procdress.cpp makes them solid: the ties across (bridgeTies + 1 of them,
// 0.2 m along, from 0.3 m down to the planks), and the log stringers (r 0.25, centred 0.55 m down; the outer two the generator's).
inline int bridgeTies(double E) { return std::max(3, (int)std::lround(2 * E / 1.6)); }
inline double bridgeTieX(double E, int t) { return -E + 0.2 + (2 * E - 0.4) * t / bridgeTies(E); }
inline int bridgeStringers(double hw) { return std::max(0, (int)std::ceil((2 * (hw - 0.3)) / 1.3) - 1) + 2; }
inline double bridgeStringerZ(double hw, int s) { return -(hw - 0.3) + 2 * (hw - 0.3) * s / (bridgeStringers(hw) - 1); }
// A bridge's wear seed from its model's two (each 1..63).
inline uint32_t bridgeSeed(int a, int b) { return (uint32_t)(a * 64 + b); }

// The mesh cache (scenery.cpp placedMesh) keeps a model's numbers to ~3%: a value as it comes back from that rounding, so what's
// drawn and what's driven on are made from the same numbers.
double modelQuant(double v);

}  // namespace proc
