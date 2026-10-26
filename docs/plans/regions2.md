# Regions grown: each of the big map's regions in kilometres of its own country

Owner's ask (2026-10-25), after trying the one map (`docs/plans/one-map.md`): the biomes weren't similar sizes (the Backcountry
~37 km2, each region ~1.4 km2), the north, east and corners were bare edge mountains, and every region drew with the Backcountry's
ground. Later in the same day: more features and paths to follow, a mountain pass with hard obstacles, far more rocks and rock
features, no empty land, big standalone boulders, a slab-shelf set piece, cabins and fire lookouts, caches. Branch `wip/regions2`.

## Layout (the map grows from 8.2 to 10.24 km: half-size 5120 m, grid 10241)

The Backcountry and the three old maps stay exactly where they were (their middles, their offsets, every coordinate). Round each old
map, a rectangle of its own country (`MapRegion::ex0..ez1`, big-map coordinates), from the Backcountry's 2650 m line out to the big
map's own edge mountains (the last 330 m):

| Region | Old map middle | Grown country | Size | Look (meadow, bare rock from, snow from) |
|---|---|---|---|---|
| Open Country | (-3496, 0) | x -4790..-2650, z -2650..2900 | 2.1 x 5.5 km (11.9 km2) | meadow (forest floor in its northern woods), 60 m, 240 m |
| Mountain | (-1500, -3496) | x -4790..-150, z -4790..-2650 | 4.6 x 2.1 km (9.9 km2, with the south-west corner) | forest ground, 165 m, 205 m |
| Frontier | (1500, -3496) | x -150..4790, z -4790..-2650 | 4.9 x 2.1 km (10.6 km2, with the south-east corner) | forest ground, 125 m, 400 m (its snow is painted) |

North and east past the Backcountry's ridge stay the big map's natural edge (high country with painted snow on the tops), now with
sparse pines, rock and bushes on it.

`regionWeights(x, z)` says how much of a point is each region's: 0 within 2650 m of the middle (the Backcountry's land is untouched
there, cell for cell), easing in over 400 m past it; neighbours share their border over 300 m. Between an old map and the Backcountry,
across the old map's width, the seam stays as the one map had it (the Backcountry's land eased into the old map's edge).

## The land (`src/regionland.cpp`)

Made the way the maps' own land is (lattice noise, smoothsteps, no new pipeline), into `backcountryLand`'s 4 m samples:

- **Mountain**: massifs about a kilometre across with ridgelines on them and broad valleys between, higher further from the old map's
  valley and toward the south-west corner (up to ~400 m, snow from 205 m, pines to ~175 m).
- **Frontier**: badlands of tiered mesas (three tiers, cliffs between), slickrock domes and sandy washes to the south and west; to the
  east a cold bench rising to peaks in the corner, cut by a canyon with a frozen creek on its floor.
- **Open Country**: rolling meadows (+-15 m), wooded hills in the north, red-rock mesas and canyons in the south (the old open country's
  canyon flavour).
- Washes and gullies wind through all three (`regionWash`: sand, gravel beds).

Each old map keeps its middle cell for cell; its rims on every side (the far side too now) give way to the country round it, its edge
carried out and faded over 300 m as before.

## The ground's look

`groundLookAt(x, z)` blends each region's look (meadow, rock height, snow height) into the Backcountry's. The renderer puts it in a
small texture (a texel per 64 m, 161 x 161, filtered: the seams blend) that the terrain shaders (photo and procedural) and the grass read
through `regionLook()`; on every other map it's off and the old uniforms decide, exactly as before. The Mountain's snow is the shader's
again (the one map had painted it on). Paint follows each region's kind (`regionGround`): snow and scree up high on the Mountain, desert,
red rock, slickrock and sand washes on the Frontier's badlands, snow and a frozen creek on its cold bench, red rock in the Open Country's
canyons.

Music and ambience: on the big map, the region you've been in for 4 s plays its old map's (soundMap), crossfading.

## Places, trails, the pass

Places (places.h, after the caches; ids `OC.*`, `MT.*`, `FR.*`; discovered as the Backcountry's): in each region an outpost (fuel,
parts, tow, save and a repair bay), a cabin or camp, a lookout out on a single great slab of rock over the drop (TABLE ROCK, EAGLE LEDGE,
SLICKROCK SHELF), a fire lookout tower, and four caches (the career's cache system: cash, parts and tools), plus the Mountain's HIGH
PASS and SNOWLINE CABIN and the Frontier's TABLETOP MESA.

Trails (`World::layRegionTrails`, laid with the roads' own way-finder and cut-and-fill, into `regionRoads` after the three roads in):
marked (posts, cairns where it's rocky), signed at both ends by rating (green, blue, black; brown for two-tracks), between each region's
places and out of its old map's edges; two-tracks to the caches; a gravel back road between the Mountain's valley and the Frontier's
badlands. Grades: trails 14%, harder trails 18%, two-tracks 25%; the way-finder takes a cliff only where there's no other way (onto a
mesa) and the cut-and-fill makes a ramp of it.

The Mountain's pass (black): West Fork Fuel -> Snowline Cabin -> the High Pass (~265 m) -> down to Table Rock in the Open Country,
4.4 km. On it: five flights of three rock ledges (0.36-0.48 m), three rock gardens, two off-camber shelves (14%), a boulder choke a little
past the top (a gap a truck's width and a bit), its steepest 60 m as a scree climb, rock walls along it and tight switchbacks where the
way-finder winds up the face. Graded to 22%; the autopilot (a built-up rig's reach) still finds its way over.

## Growth and rock (`World::growRegionCountry`)

Every 64 m cell past the Backcountry's 6 km square, its own random numbers: lone trees, groves and woods (pines up high, broadleaf in the
valleys and meadows, small trees on the badlands), bushes in clumps (each region's kinds), fallen logs, scattered boulders (more on the
steep), a big standalone boulder (car-sized and up) in many cells, and now and then a feature: a boulder field, an outcrop, a tor, a
slab lying with the slope. The scattered scenery boulders keep a single hull for the physics (`kWholeRockSeeds`): cut into convex pieces
as the Backcountry's are, 10 000 big rocks took 1.5 GB.

## Measured (2026-10-25, Ryzen 9 9900X, other agents' load 10-25; game runs offscreen on the iGPU, not the 7900 XTX)

| | One map (8.2 km) | Regions grown (10.24 km) |
|---|---|---|
| World build, headless, code rocks / scanned rocks | 3.2 s / 4.0 s | 4.4-5.0 s / 5.0-6.4 s; after the speed-ups ~1.2x the 8 km map's, back to back under the same load (4.07 vs 3.42 s) |
| RAM the world adds, code rocks / scanned rocks | 612 / 1095 MB | ~1000 / ~1600 MB |
| Trees, rocks, bushes | 72k, 11.5k, 79k | 164k, 42.6k, 155k |
| World cache file | 82 MB | 107 MB |
| Game RSS at the Mountain, F3 (warm cache) | ~1.75 GB | ~2.0-2.5 GB (first launch peak 3.2 GB while the cache is written) |
| Renderer ready, cold | 3.0 s | 5.0 s |
| Bench at the trailhead (iGPU), GPU stages summed | 47.8 ms | 48.6 ms (TERRAIN cpu 0.26 -> 0.39 ms) |
| Bench at the Mountain's start (iGPU), CPU stages | VEGETATION 0.25, PROPS 0.27, SHADOWS 0.31 ms | 0.51, 0.53, 0.54 ms (the country round it is grown now) |

The scattered scenery boulders as single hulls saved 1.2 GB (10 000 big rocks cut into pieces took 1.5 GB). Still to measure in a
real window on the 7900 XTX (paused while the owner played): the frame time at the trailhead and at each region's start, against
`docs/hardware/linux-7900xtx-onemap.md`.

Land hash `1bb9ed3935450d1c` (headless, code rocks) on Linux and on the Windows build under wine. Physics fingerprint unchanged
(`da17a95524fab674`).

## Multiplayer

The map id is the same; every machine builds the same land from it (`onemap/same-every-time`: one thread or many, through the cache).
`net::kVersion` stays 9: 9 hasn't shipped (kRelease 0.4.7 is the first that speaks it), so the one map and this go out together. If
0.4.7 ships the 8 km map without this, bump it.
