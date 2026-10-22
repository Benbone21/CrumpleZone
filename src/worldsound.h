// Sounds that come from places in the world: the repair shops' workshop din and the water (a river's rush, a stream's trickle, a
// pond's lap), each from the nearest point of it, plus a daytime forest bed. Game thread only: it finds the sources once per world,
// then each frame picks the loudest few for the listener and hands their gain and pan to Audio::setWorldVoice (the callback does the rest).
#pragma once
#include "world.h"
#include <vector>

enum WorldLoop { WL_SHOP, WL_RIVER, WL_STREAM, WL_FOREST, WL_COUNT };

class WorldSounds {
public:
    static constexpr int kShopVoices = 2, kWaterVoices = 2, kVoices = kShopVoices + kWaterVoices + 1;  // (+ the forest bed, the last)
    struct Voice { int loop = -1; float gain = 0, pan = 0; double dist = 0; };
    struct Body {
        int loop = WL_STREAM;
        float loud = 0, range = 0;  // level when you're at it, and how far away (m) it can still be heard
        bool disc = false;          // a lake: nearest point of the circle (c, r); else the nearest of pts
        V3 c; double r = 0;
        std::vector<V3> pts;        // (a 3 m scatter over the wet cells)
        double cells = 0, speed = 0;
    };
    // Looks the world over (again when it changed). Cheap to call every frame.
    void prepare(const World& world);
    // The shop voices then the water voices (loop -1: silent) for a listener at `at` whose right-hand direction is `right`.
    void update(const V3& at, const V3& right, Voice out[kVoices - 1]) const;
    static Voice forest(int biome, float hours);  // the daytime forest bed (only in the pine woods)
    // 1 at the source, falling to 0 at `range` m (about -12 dB at a third of the way).
    static float falloff(double distance, double range);
    const std::vector<V3>& shops() const { return shops_; }
    const std::vector<Body>& bodies() const { return bodies_; }
private:
    void build(const World& world);
    std::vector<V3> shops_;
    std::vector<Body> bodies_;
    const World* world_ = nullptr;
    uint64_t signature_ = 0;
};
