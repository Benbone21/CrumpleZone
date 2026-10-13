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

class Effects {
public:
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
    void clear();

private:
    enum Kind : uint8_t { SMOKE, DUST, SPARK, SHARD, CLOD };  // CLOD: mud, sand, snow or gravel thrown by a tire
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
    };
    struct Track {
        bool active = false;
        V3 p, side, n;
        float along = 0, depth = 0, lip = 0;
        int segs = 0;  // segments laid since this strip started (its lips rise over the first few)
    };

    std::vector<Particle> particles;
    std::vector<Rut> skids;  // a ring: the oldest are overwritten (and fade out first)
    size_t skidHead = 0;
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
    void addSkid(Track& t, RutKind kind, const V3& p, const V3& n, double halfWidth, double depth, double lip,
                 const float tint[3], float alpha, double alongScale);
    float rutFade(size_t i) const;  // 0..1: the oldest marks fade away before they're overwritten
};
