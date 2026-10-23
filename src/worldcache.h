#pragma once
// A built map saved whole, so the next run reads it instead of building it again (docs/plans/big-maps.md, 3.3): the land
// (heights, surfaces, paint, roads, water) and everything on it, exactly as the build left it. Kept with the mesh cache
// (meshcache.h: one file per map, world-N.bin, holding the build keyed by the game build, the map, its size and the scanned rocks in use), so a new
// build, another map size or other rock scans never find an old one, and a damaged file is just a miss.
#include <cstdint>
#include <vector>

class World;

namespace worldcache {

// Whether World(map) goes through the cache for this map (the big hand-made maps: their build is the slow part of a load).
// Off with the mesh cache off (CZ_NO_MESH_CACHE), with a test's map size, and on this thread while `bypass` is set.
bool cacheable(int map);
inline thread_local bool bypass = false;

// The key a map's build is stored under (0: not cacheable).
uint64_t keyFor(int map);

// The world as bytes, and back (false: not a world, or a damaged one; `w` is then left as it was).
std::vector<uint8_t> encode(const World& w);
bool decode(const std::vector<uint8_t>& bytes, World& w);

// World's constructor: fill `w` (its map and size set) from the cache; and store a fresh build.
bool load(World& w);
void store(const World& w);

}  // namespace worldcache
