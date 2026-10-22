// Visual effects driven by the simulation: tire smoke and dust, sparks from
// scraping metal, glass shards, engine smoke, skid marks and tire tracks.
#pragma once
#include <cstdint>
#include <vector>

#include "mathx.h"
#include "softbody.h"

struct Vtx;

// Things that happened this frame, for the audio.
struct FxEvents {
    int glass = 0;     // windows shattered
    int tirePops = 0;  // tires blown
};

// How long tire tracks and skid marks last, how far they're drawn and how many are kept, by quality
// preset (perf.h QualityPreset: LOW MEDIUM HIGH ULTRA POTATO; CUSTOM counts as MEDIUM).
struct MarkLimits {
    size_t cap;       // most marks kept: a ring, the oldest go first (a mark is ~20 cm of one tire's track)
    float life;       // seconds before a mark has faded out
    float fadeDist;   // m: marks are gone past this, fading in over the last 30%
};
MarkLimits markLimits(int preset);
constexpr size_t kMaxMarkCap = 16000;  // (the ring's storage: the largest cap)

struct MarkStats {
    size_t marks = 0, drawn = 0;  // alive; and how many of those the last buildSkids drew
    size_t bytes = 0;             // ring storage
};

class Effects {
public:
    void setLimits(const MarkLimits& m);  // the quality changed
    MarkStats markStats() const;
    // The i-th oldest live mark (tests, debugging): where it starts, how wide it is, whether it's black rubber.
    struct MarkInfo { V3 p; double width; bool rubber; double age; };
    MarkInfo markInfo(size_t i) const;
    double clock() const { return clock_; }  // simulated seconds since the last clear (marks age by it)
    // simDt is the simulated time that passed this frame (0 while paused, small in slow motion).
    FxEvents update(Sim& sim, double simDt);
    // Appends the tire tracks and skid marks within sight of `eye`: decal strips laid on
    // the ground, drawn multiplied into it (fx shader modes 3 and 4), so they're lit like
    // the ground under them by day, at night and in shadow.
    void buildSkids(const V3& eye, std::vector<Vtx>& out) const;
    // Appends the ground a tire has shoved aside, as terrain-painted geometry for the lit
    // shader (material 7): the lips along fresh ruts in sand, mud and snow near `eye`, and
    // a collar round every tire sunk into it, so a sunk tire sits down in a hole.
    void buildRutWalls(const Sim& sim, const V3& eye, std::vector<Vtx>& out) const;
    void translate(const V3& d);  // the freeway moved everything back a period
    void buildParticles(const V3& eye, std::vector<Vtx>& out);
    // The main menu's camp fire at `base` (the ground under the logs): this frame's flames, sparks and smoke, drifting with `wind`.
    // (The particles age in update(): the menu feeds it the real frame time, as the sim is paused behind the menu.)
    void campfire(const V3& base, double dt, const V3& wind);
    void clear();

private:
    enum Kind : uint8_t { SMOKE, DUST, SPARK, SHARD, CLOD, FLAME };  // CLOD: mud, sand, snow or gravel thrown by a tire
    struct Particle {
        V3 p, v;
        float age = 0, life = 1, size0 = 0.1f, size1 = 0.1f;
        float col[4];
        Kind kind;
    };
    // One stretch of a tire track or skid mark, from end 0 (older) to end 1.
    enum RutKind : uint8_t { RUT_GROOVE, RUT_RUBBER };
    struct Rut {
        V3 p[2];          // center line, on the ground
        V3 side[2];       // half the mark's width, across it along the ground
        V3 n[2];          // ground normal
        float along[2];   // distance along the track (m, wrapped every 64 m): the tread print
        float depth[2];   // how far the tire was down in the ground (m)
        float lip[2];     // height of the ground it shoved up either side (m; 0 = none)
        float tint[3];    // what the mark does to the ground's color, at full strength
        float alpha;
        RutKind kind;
        float born;       // clock_ when laid
        float look;       // shader tread code: tire type, smear and direction (packed, see fx.cpp)
    };
    struct Track {
        bool active = false;
        V3 p, side, n;
        float along = 0, depth = 0, lip = 0;
        float roll = 0;  // tire surface travel since the last mark (m, signed)
        int segs = 0;  // segments laid since this strip started (its lips rise over the first few)
    };

    std::vector<Particle> particles;
    std::vector<Rut> skids;  // a ring (storage never grows past kMaxMarkCap): the oldest go first, fading out before they do
    size_t skidHead = 0, skidCount = 0;  // (oldest, how many alive)
    MarkLimits lim_ = markLimits(1);
    double clock_ = 0;
    mutable size_t lastDrawn_ = 0;
    const Rut& markAt(size_t i) const { return skids[(skidHead + i) % skids.size()]; }  // i-th oldest
    float markFade(size_t i, const Rut& r, const V3& eye, double far) const;  // 0..1: age, ring position and distance
    std::vector<std::vector<Track>> tracks;  // [car][wheel]
    std::vector<const Car*> trackOwner;
    uint32_t rng = 0x9E3779B9u;

    float rand01() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng & 0xFFFFFF) / float(0x1000000);
    }
    float randS() { return rand01() * 2 - 1; }
    // Emits floor(rate * dt) particles plus one more with the leftover probability.
    int count(double rate, double dt) {
        double n = rate * dt;
        int whole = (int)n;
        return whole + (rand01() < n - whole ? 1 : 0);
    }
    void emit(Kind kind, const V3& p, const V3& v, float life, float s0, float s1, float r, float g, float b, float a);
    // Extends a wheel's track to ground point p: a new segment every ~20 cm.
    void addSkid(const World& world, Track& t, RutKind kind, const V3& p, const V3& n, double halfWidth, double depth, double lip,
                 const float tint[3], float alpha, float look);
    void push(const Rut& r);
};
