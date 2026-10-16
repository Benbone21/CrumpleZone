// Which kind of country the player is in, for the music (music.h gives each its own style).
// Worked out from what the world already stores: the obstacle sections, water, the ground's
// surface and paint, how high it is and which trees stand near. Cheap enough to ask twice a
// second; BiomeTracker adds the hysteresis so driving along a border doesn't flip-flop.
#pragma once

enum Biome {
    BIOME_DEFAULT,  // meadow, road, town, anything else: the map's own piece
    BIOME_DESERT,   // sand and dunes: airy and dry
    BIOME_PINE,     // pine woods: warm and woody
    BIOME_CANYON,   // red rock and cliffs: tense, long echo
    BIOME_RIVER,    // rivers, lakes, mud and wetland: flowing
    BIOME_ALPINE,   // snow and high bare ground: sparse and bright
    BIOME_COUNT
};
inline const char* biomeName(int biome) {
    static const char* names[BIOME_COUNT] = {"default", "desert", "pine", "canyon", "river", "alpine"};
    return biome >= 0 && biome < BIOME_COUNT ? names[biome] : "default";
}

class World;
Biome biomeAt(const World& w, double x, double z);

// Smooths biomeAt over time. A new biome has to hold for kDwell seconds before it is taken up,
// and a change is followed by kHold seconds with no other (the music crossfades meanwhile).
struct BiomeTracker {
    static constexpr double kPoll = 0.5, kDwell = 3.0, kHold = 14.0;
    Biome current = BIOME_DEFAULT;
    // Call every frame; it looks at the ground twice a second. A new map snaps straight to its biome.
    Biome update(const World& w, double x, double z, double dt);
    // The hysteresis alone: one reading `dt` seconds after the last.
    Biome feed(Biome raw, double dt);
    void reset() { started_ = false; }
private:
    Biome pending_ = BIOME_DEFAULT;
    double pendingFor_ = 0, sinceChange_ = 1e9, sincePoll_ = 1e9;
    int map_ = -1;
    bool started_ = false;
};
