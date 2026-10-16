#include "biome.h"

#include <cmath>

#include "sections.h"
#include "world.h"

namespace {
// What each obstacle section sounds like.
Biome sectionBiome(int type) {
    switch (type) {
        case SEC_STEEP_HILL: return BIOME_ALPINE;   // bare scree, high and open
        case SEC_MUD_PIT: return BIOME_RIVER;
        case SEC_ROCKY_RIVER: return BIOME_RIVER;
        case SEC_ROCK_WALL: return BIOME_CANYON;
        case SEC_DUNES: return BIOME_DESERT;
        case SEC_CLIFF_SHELF: return BIOME_CANYON;
        case SEC_TIMBER: return BIOME_PINE;
    }
    return BIOME_DEFAULT;
}
}  // namespace

Biome biomeAt(const World& w, double x, double z) {
    if (!std::isfinite(x) || !std::isfinite(z) || w.map == MAP_FREEWAY) return BIOME_DEFAULT;
    // Inside an obstacle section's ground (the trail, its run-in and run-out).
    for (const World::SectionSpot& s : w.sections) {
        const SectionFootprint fp = sectionFootprint((SectionType)s.type);
        const double dx = x - s.entry.x, dz = z - s.entry.z;
        const double u = dx * std::cos(s.heading) - dz * std::sin(s.heading);
        const double v = dx * std::sin(s.heading) + dz * std::cos(s.heading);
        if (u > -fp.apron && u < fp.length + fp.apron && std::fabs(v) < fp.half) return sectionBiome(s.type);
    }
    const double h = w.height(x, z);
    if (w.waterLevel(x, z) > h - 0.2) return BIOME_RIVER;
    const Surface surf = w.surfaceAt(x, z);
    switch (surf) {
        case SURF_SAND: return BIOME_DESERT;
        case SURF_SNOW: case SURF_ICE: return BIOME_ALPINE;
        case SURF_MUD: case SURF_WET: return BIOME_RIVER;
        case SURF_PAVED: return BIOME_DEFAULT;  // roads and the pad
        default: break;
    }
    const int cell = w.cellIndex(x, z);
    const int paint = cell >= 0 ? w.cellPaint(cell) : PAINT_NONE;
    if (paint == PAINT_REDROCK) return BIOME_CANYON;
    if (paint == PAINT_DESERT) return BIOME_DESERT;
    if (paint == PAINT_SNOW || paint == PAINT_ICE) return BIOME_ALPINE;
    if (w.map == MAP_MOUNTAIN && h > 150) return BIOME_ALPINE;  // above the trees
    // Woods: pines standing within a stone's throw, and more of them than broadleaves.
    int pines = 0, broad = 0;
    constexpr double R2 = 22.0 * 22.0;
    for (const Tree& t : w.trees) {
        const double dx = t.base.x - x, dz = t.base.z - z;
        if (dx * dx + dz * dz > R2) continue;
        if (t.kind == 0) pines++;
        else if (t.kind == 1) broad++;
    }
    if (pines >= 5 && pines * 2 >= broad * 3) return BIOME_PINE;
    return BIOME_DEFAULT;
}

Biome BiomeTracker::feed(Biome raw, double dt) {
    sinceChange_ += dt;
    if (raw == current) {
        pending_ = current;
        pendingFor_ = 0;
        return current;
    }
    if (raw != pending_) {
        pending_ = raw;
        pendingFor_ = 0;
    }
    pendingFor_ += dt;
    if (pendingFor_ >= kDwell && sinceChange_ >= kHold) {
        current = pending_;
        pendingFor_ = 0;
        sinceChange_ = 0;
    }
    return current;
}

Biome BiomeTracker::update(const World& w, double x, double z, double dt) {
    if (!started_ || w.map != map_) {  // a new map: no fade from the last place
        started_ = true;
        map_ = w.map;
        sincePoll_ = 0;
        current = pending_ = biomeAt(w, x, z);
        pendingFor_ = 0;
        sinceChange_ = kHold;
        return current;
    }
    sincePoll_ += dt;
    if (sincePoll_ < kPoll) return current;
    const double elapsed = sincePoll_;
    sincePoll_ = 0;
    return feed(biomeAt(w, x, z), elapsed);
}
