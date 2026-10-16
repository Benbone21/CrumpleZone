// Biome music (biome.h, music.h): the biome lookup finds the right country at known spots, every
// biome's piece renders finite, audible and unclipped at about the level of the maps' own, and the
// change from one to the next is steadied (dwell, hold) and crossfaded rather than flipping.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../src/biome.h"
#include "../src/music.h"
#include "../src/sections.h"
#include "harness.h"

namespace {

struct Level {
    double rms = 0;
    float peak = 0;
    bool finite = true;
    uint64_t hash = 1469598103934665603ull;
};

Level renderPiece(int map, int biome, int seconds, uint32_t seed = 701) {
    Music music;
    music.setSeed(seed);
    Level lv;
    std::vector<float> block(1024);
    double e = 0;
    long n = 0;
    for (int at = 0; at < seconds * 48000; at += 512) {
        std::fill(block.begin(), block.end(), 0.0f);
        music.render(block.data(), 512, map, 0.65f, biome);
        for (float x : block) {
            lv.finite = lv.finite && std::isfinite(x);
            lv.peak = std::max(lv.peak, std::fabs(x));
            e += (double)x * x, n++;
            uint32_t b;
            std::memcpy(&b, &x, 4);
            lv.hash = (lv.hash ^ b) * 1099511628211ull;
        }
    }
    lv.rms = std::sqrt(e / std::max(1L, n));
    return lv;
}

// The middle of a section's trail.
V3 midOf(const World::SectionSpot& s) { return (s.entry + s.exit) * 0.5; }

}  // namespace

SCENARIO(biomeLookup, "music/biome-lookup", "The biome under a spot: dunes, rock wall, river, timber and scree sections, forest, meadow, the spawn pad and the freeway") {
    const bool was = World::withShowcase;
    World::withShowcase = true;
    const World w(MAP_PROVING_GROUND);
    World::withShowcase = was;
    ctx.check("seven sections to look at", w.sections.size() == SEC_COUNT, std::to_string(w.sections.size()));
    auto at = [&](int type) {
        for (const World::SectionSpot& s : w.sections)
            if (s.type == type) return biomeAt(w, midOf(s).x, midOf(s).z);
        return BIOME_COUNT;
    };
    ctx.check("inside the dunes: desert", at(SEC_DUNES) == BIOME_DESERT, biomeName(at(SEC_DUNES)));
    ctx.check("on the rock wall: canyon", at(SEC_ROCK_WALL) == BIOME_CANYON, biomeName(at(SEC_ROCK_WALL)));
    ctx.check("on the cliff shelf: canyon", at(SEC_CLIFF_SHELF) == BIOME_CANYON, biomeName(at(SEC_CLIFF_SHELF)));
    ctx.check("in the rocky river: river", at(SEC_ROCKY_RIVER) == BIOME_RIVER, biomeName(at(SEC_ROCKY_RIVER)));
    ctx.check("in the mud pit: river (wetland)", at(SEC_MUD_PIT) == BIOME_RIVER, biomeName(at(SEC_MUD_PIT)));
    ctx.check("on the timber track: pine", at(SEC_TIMBER) == BIOME_PINE, biomeName(at(SEC_TIMBER)));
    ctx.check("on the steep scree hill: alpine", at(SEC_STEEP_HILL) == BIOME_ALPINE, biomeName(at(SEC_STEEP_HILL)));
    ctx.check("at the spawn: the map's own music", biomeAt(w, w.spawn.x, w.spawn.z) == BIOME_DEFAULT, biomeName(biomeAt(w, w.spawn.x, w.spawn.z)));
    for (const V3& p : w.parking) ctx.check("at the parking: the map's own music", biomeAt(w, p.x, p.z) == BIOME_DEFAULT);
    for (const V3& p : w.loopRoad) {
        if (biomeAt(w, p.x, p.z) != BIOME_DEFAULT) {
            ctx.check("the loop road is the map's own music", false, std::to_string(p.x) + "," + std::to_string(p.z) + " " + biomeName(biomeAt(w, p.x, p.z)));
            break;
        }
    }
    // The woods: a stand of trees planted in an open meadow (found below), pines then broadleaves.
    const V3 spot(-120, 0, 330);
    World grove(MAP_PROVING_GROUND);
    ctx.check("the test meadow is open grass", grove.surfaceAt(spot.x, spot.z) == SURF_GRASS && grove.waterLevel(spot.x, spot.z) < -1e8 && biomeAt(grove, spot.x, spot.z) == BIOME_DEFAULT);
    for (int k = 0; k < 14; k++) grove.addTree(spot.x + 12 * std::cos(k * 0.45), spot.z + 12 * std::sin(k * 0.45), 11, 1);
    ctx.check("a grove of broadleaves is not the pine woods", biomeAt(grove, spot.x, spot.z) == BIOME_DEFAULT, biomeName(biomeAt(grove, spot.x, spot.z)));
    for (int k = 0; k < 24; k++) grove.addTree(spot.x + 16 * std::cos(k * 0.26 + 0.2), spot.z + 16 * std::sin(k * 0.26 + 0.2), 11, 0);
    ctx.check("in a mixed wood with more pines: pine", biomeAt(grove, spot.x, spot.z) == BIOME_PINE, biomeName(biomeAt(grove, spot.x, spot.z)));
    World pines(MAP_PROVING_GROUND);
    for (int k = 0; k < 8; k++) pines.addTree(spot.x + 18 * std::cos(k * 0.8), spot.z + 18 * std::sin(k * 0.8), 11, 0);
    ctx.check("in a pine wood: pine", biomeAt(pines, spot.x, spot.z) == BIOME_PINE);
    ctx.check("and well out of it, the map's own", biomeAt(pines, spot.x + 90, spot.z) == BIOME_DEFAULT);
    pines.addTree(spot.x + 90, spot.z, 11, 0);
    ctx.check("a lone pine is not a wood", biomeAt(pines, spot.x + 90, spot.z) == BIOME_DEFAULT);
    // An open meadow: grass, no tree within 60 m, dry, outside every section.
    int meadows = 0, meadowOk = 0;
    for (double x = -380; x <= 380 && meadows < 30; x += 30)
        for (double z = -380; z <= 380 && meadows < 30; z += 30) {
            if (std::hypot(x, z) < 160 || w.surfaceAt(x, z) != SURF_GRASS || w.waterLevel(x, z) > -1e8) continue;
            bool clear = true;
            for (const Tree& t : w.trees) clear = clear && std::hypot(t.base.x - x, t.base.z - z) > 60;
            for (const World::SectionSpot& s : w.sections) clear = clear && std::hypot(x - midOf(s).x, z - midOf(s).z) > 160;
            if (!clear) continue;
            meadows++;
            meadowOk += biomeAt(w, x, z) == BIOME_DEFAULT;
        }
    ctx.check("there are meadows", meadows >= 3, std::to_string(meadows));
    ctx.check("in the open meadow: the map's own music", meadowOk == meadows, std::to_string(meadowOk) + " of " + std::to_string(meadows));
    ctx.check("nonsense positions are the map's own", biomeAt(w, NAN, 0) == BIOME_DEFAULT && biomeAt(w, 1e9, -1e9) == BIOME_DEFAULT);
    const World freeway(MAP_FREEWAY);
    ctx.check("the freeway keeps its own", biomeAt(freeway, 0, 0) == BIOME_DEFAULT && Music::pieceFor(MAP_FREEWAY, BIOME_PINE) == MAP_FREEWAY);
    // A new map snaps straight to its biome, no waiting.
    BiomeTracker bt;
    const V3 d = midOf(w.sections[SEC_DUNES]);
    bt.update(w, d.x, d.z, 0.016);
    ctx.check("a tracker starts in the biome it finds", bt.current == BIOME_DESERT, biomeName(bt.current));
}

SCENARIO(biomeHysteresis, "music/biome-hysteresis", "A new biome has to hold before the music takes it up, a border doesn't flip it, and the crossfade drifts") {
    BiomeTracker bt;
    bt.feed(BIOME_DEFAULT, 20);  // settled
    // Driving along a border: the readings alternate every half second.
    int changes = 0;
    Biome last = bt.current;
    for (int i = 0; i < 240; i++) {
        bt.feed(i % 2 ? BIOME_PINE : BIOME_DEFAULT, 0.5);
        changes += bt.current != last, last = bt.current;
    }
    ctx.check("a flickering border never changes the music", changes == 0, std::to_string(changes));
    // A short dip into another biome and back: nothing.
    for (int i = 0; i < 4; i++) bt.feed(BIOME_RIVER, 0.5);  // 2 s of river
    for (int i = 0; i < 20; i++) bt.feed(BIOME_DEFAULT, 0.5);
    ctx.check("a two-second dip into a river is ignored", bt.current == BIOME_DEFAULT);
    // Staying: taken up after the dwell, not before.
    double t = 0;
    while (t < BiomeTracker::kDwell - 0.6) bt.feed(BIOME_DESERT, 0.5), t += 0.5;
    ctx.check("not yet before the dwell", bt.current == BIOME_DEFAULT);
    while (bt.current != BIOME_DESERT && t < 10) bt.feed(BIOME_DESERT, 0.5), t += 0.5;
    ctx.check("taken up after the dwell", bt.current == BIOME_DESERT && t >= BiomeTracker::kDwell && t <= BiomeTracker::kDwell + 1, std::to_string(t));
    // Straight away the other way: held until the crossfade is done.
    double held = 0;
    while (bt.current == BIOME_DESERT && held < 30) bt.feed(BIOME_PINE, 0.5), held += 0.5;
    ctx.check("a change is held while the music crossfades", held >= BiomeTracker::kHold - 0.5 && held <= BiomeTracker::kHold + 1, std::to_string(held));
    ctx.check("then the new biome takes over", bt.current == BIOME_PINE);

    // The music itself: asking for a biome crossfades over 8 s (the old piece is still sounding
    // halfway), and rapid requests don't restart the pieces over and over.
    Music music;
    music.setSeed(5);
    std::vector<float> block(1024);
    auto run = [&](double seconds, int biome) {
        for (int at = 0; at < (int)(seconds * 48000); at += 512) {
            std::fill(block.begin(), block.end(), 0.0f);
            music.render(block.data(), 512, 0, 0.65f, biome);
        }
    };
    run(3, BIOME_DESERT);
    const int desertPiece = music.piece();
    ctx.check("the desert's piece plays in the desert", desertPiece == Music::pieceFor(0, BIOME_DESERT) && desertPiece != 0);
    int switches = 0, lastPiece = music.piece();
    bool finite = true;
    for (int i = 0; i < 40; i++) {  // alternate every second for 40 s
        for (int at = 0; at < 48000; at += 512) {
            std::fill(block.begin(), block.end(), 0.0f);
            music.render(block.data(), 512, 0, 0.65f, i % 2 ? BIOME_ALPINE : BIOME_DESERT);
            for (float x : block) finite = finite && std::isfinite(x) && std::fabs(x) <= 1;
        }
        switches += music.piece() != lastPiece, lastPiece = music.piece();
    }
    ctx.check("rapid requests change the piece at most every crossfade", switches <= 6, std::to_string(switches));
    ctx.check("and stay bounded through them", finite);
    // Mid-crossfade the outgoing piece is still there: the level stays up through the change.
    Music mid;
    mid.setSeed(9);
    double before = 0, during = 0;
    for (int at = 0; at < 6 * 48000; at += 512) {
        std::fill(block.begin(), block.end(), 0.0f);
        mid.render(block.data(), 512, 0, 0.65f, BIOME_PINE);
        if (at > 4 * 48000)
            for (float x : block) before += (double)x * x;
    }
    for (int at = 0; at < 8 * 48000; at += 512) {
        std::fill(block.begin(), block.end(), 0.0f);
        mid.render(block.data(), 512, 0, 0.65f, BIOME_ALPINE);
        if (at > 3 * 48000 && at < 5 * 48000)
            for (float x : block) during += (double)x * x;
    }
    ctx.check("the music doesn't dip out mid-crossfade", during > before * 0.35 && during < before * 3, std::to_string(during / std::max(before, 1e-9)));
}

SCENARIO(biomeMusic, "music/biome-pieces", "Each biome's music renders finite, audible, unclipped, at the level of the maps' own, and differs from the rest") {
    const Level own = renderPiece(0, 0, 30);
    ctx.check("the map's own piece is audible", own.finite && own.rms > 0.03);
    std::vector<uint64_t> hashes{own.hash};
    for (int b = 1; b < BIOME_COUNT; b++) {
        const Level lv = renderPiece(0, b, 30);
        const std::string tag = std::string(biomeName(b)) + ": ";
        ctx.check(tag + "finite", lv.finite);
        ctx.check(tag + "audible", lv.rms > 0.03, std::to_string(lv.rms));
        ctx.check(tag + "not clipping", lv.peak < 0.7f, std::to_string(lv.peak));
        ctx.check(tag + "about as loud as the map's own", lv.rms > own.rms * 0.55 && lv.rms < own.rms * 1.45, std::to_string(lv.rms) + " vs " + std::to_string(own.rms));
        ctx.check(tag + "steady from run to run", renderPiece(0, b, 10).hash == renderPiece(0, b, 10).hash);
        ctx.check(tag + "differs from the others", std::find(hashes.begin(), hashes.end(), lv.hash) == hashes.end());
        hashes.push_back(lv.hash);
    }
    // The maps' own pieces are untouched by the biome pieces, and the freeway has no biome music.
    ctx.check("biome 0 is the map's own piece", renderPiece(3, 0, 5).hash == renderPiece(3, BIOME_DEFAULT, 5).hash);
    ctx.check("the freeway ignores the biome", renderPiece(2, BIOME_ALPINE, 5).hash == renderPiece(2, 0, 5).hash);
    // Invalid input is quiet and finite.
    Music m;
    std::vector<float> block(1024, 0.0f);
    m.render(block.data(), 512, 0, 0.5f, 99);
    m.render(block.data(), 512, 0, 0.5f, -3);
    bool ok = true;
    for (float x : block) ok = ok && std::isfinite(x);
    ctx.check("a bad biome number is harmless", ok);
}
