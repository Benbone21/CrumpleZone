# One map: the Backcountry with the Mountain, the Frontier and the Open Country stitched on

Grown since (2026-10-25, `wip/regions2`): the map is 10.24 km and each region has kilometres of its own country round it: `docs/plans/regions2.md`.

Owner's ask (2026-10-25): "stitch all the current maps together except Proving Ground and Freeway, so we have one big map."
Branch: `wip/onemap`. Status: phases 1-4 built (section 9 says what changed from the plan); phase 5 (measurements) in `docs/hardware/linux-7900xtx-onemap.md`.

## Summary (plain words)

1. The 6 x 6 km Backcountry stays the middle of the map, exactly where it is. Three of the old maps become regions on its edges:
   **Open Country** in the west, **Mountain** in the south-west and **Frontier** in the south-east. Each keeps its own land, trails,
   stations, lakes and rivers, moved over whole (only moved sideways, never up or down and never turned).
2. The map grows from 6.1 km to **8.2 km** square (half-size 4096 m). The Backcountry's edge ridge opens into a valley where a region meets
   it, and the land eases from one into the other over about 350 m: no walls, no cliffs. Elsewhere the edge stays mountains.
3. Roads meet: a gravel road runs from the Backcountry into each region (Ranger Flats to the Open Country, Lakeshore to the Mountain's
   valley loop, the Lost Mine's wash to the Frontier's Cold Creek Lodge). Elk River, which used to run off the west edge, ends in a lake
   on the seam with the Open Country.
4. The map list keeps PROVING GROUND and FREEWAY as they are. BACKCOUNTRY is the big map, and MOUNTAIN, FRONTIER and OPEN COUNTRY become
   start points on it ("BACKCOUNTRY: MOUNTAIN" starts you at the Mountain's trailhead). Map ids don't change, so saves, settings and
   the career's world saves keep working.
5. Old level files made on the Mountain, the Frontier or the Open Country play in their region of the big map, moved over the same way.
   The level files themselves are not rewritten, and the editor still edits them on their own small maps.
6. Multiplayer: every machine builds the same big map from the map id. The Backcountry's map changes for the same id, so the network
   version goes up (8 to 9).
7. Targets: within about 1.5x today's 6 km Backcountry in load time and RAM, and the same frame rate (it uses the existing streaming).

## 1. Where we are (measured 2026-10-25 on main @ ed0249b, Ryzen 9 9900X, RX 7900 XTX, load average 13-19 from other agents)

`./crumplezone --no-audio --no-pad --map 4 --quality 2 --bench 600,120` with a private world cache:

| | Wall time (launch to the end of 720 frames) | Peak RSS | Renderer ready | Bench average (HIGH 1600x900) | 1% low |
|---|---|---|---|---|---|
| Cold (no cache) | 13.7 s | 1.21 GB | 3.2 s | 5.40 ms | 13.1 ms |
| Warm (cache) | 7.1 s | 1.04 GB | 2.0 s | 5.54 ms | 17.1 ms |

The old maps' edges, from `World(map).height()` (m, min..max and the mean along each side, 500 m out from the middle):

| Map | North | South | East | West |
|---|---|---|---|---|
| Mountain | -3..71 (26): the massif's foot, low at both corners | 0 (valley) | 0..22 | 0..26 |
| Frontier | 33..176 (118): the range, low (35) at the north-east corner | 1..7 (4) | 1..52 (14) | 2..68 (14) |
| Open Country | 5..16 (11) | 1..22 | 3..18 | 1..23 |
| Backcountry (at 2700 m, before its edge ridge) | 37..175 (105) | 8..56 (28) | 20..172 (88) | -15..54 (22) |

So the Backcountry is low on its west and south sides and high in the north and east. The regions go where the heights already match.
Nothing is moved up or down: a region's heights stay what they were, so its trails, its old level files and the ground shader's
rock and snow heights stay right.

## 2. Layout

The combined map: x and z from -4096 to +4096 (grid 8193 x 8193 at 1 m; 1.78x the cells of the 6 km map). Each region is its old
1.2 km map placed whole, its middle at:

| Region | Old map id | Middle (x, z) | Covers | Faces the Backcountry with | Meets |
|---|---|---|---|---|---|
| Open Country | MAP_WILDS (5), meadow | (-3496, 0) | x -4096..-2896, z -600..600 | its east side | Ranger Flats; Elk River's mouth lake |
| Mountain | MAP_MOUNTAIN (1) | (-1500, -3496) | x -2100..-900, z -4096..-2896 | its north side (the massif's foot, low at the corners) | Lakeshore and the south uplands |
| Frontier | MAP_FRONTIER (3) | (1500, -3496) | x 900..2100, z -4096..-2896 | its north side (the range; Cold Creek's valley at its north-east corner) | Badlands Wash |

A point (x, z) on an old map is at (x + mx, z + mz) on the big one, where (mx, mz) is its middle above. Heights are unchanged.

What stays the Backcountry: everything within 2650 m of the middle (all its places: the furthest cache is at 2567 m). The rest of
the square (north, east and the corners beyond 3072 m) is the Backcountry's edge ridge, carried on out: bare high mountains to look at.

## 3. Seams

Each region has a trusted core: the whole 1.2 km square except the strip on the side that faces the Backcountry where its old rim
mountains rose (the last 95 m on the Mountain and the Frontier, the last 135 m on the Open Country, whose hills close in sooner).

- **Heights.** Inside the core the region's heights are copied cell for cell. Between the core's edge and the Backcountry's own land
  2650 m out, the land is a blend: `lerp(backcountry, region edge, w)`, where `w` eases from 0 to 1 over the band (about 340-380 m),
  and "region edge" is the core's edge heights carried straight out, smoothed (a 40 m average) the further they're carried. The
  Backcountry's edge ridge is flattened along each region's width (tapering back over 200 m beyond it), so the seam is a broad valley
  with the region's own rims and the ridge as its walls. On a region's other sides, its own rim mountains meet the Backcountry's edge
  ridge, blended over 150 m.
- **Slope bound.** The steepest the blend adds is about 1.5 x (height difference) / (band width): 0.1-0.25 where the heights match
  (west, the Mountain's corners, the Frontier's Cold Creek corner). Where the Frontier's range meets the seam the land rises as a
  mountainside (that part was a range already). Tested: section 7.
- **Surfaces, paint and growth.** The core keeps the region's own (surfaces, paint, roads, water). The band is the Backcountry's: it is
  laid out before the Backcountry paints and plants, so its paint (rock on the steep, grass), trees, rocks and bushes follow the blended
  land. The region's own trees and rocks are kept in its core; the few that stood on its rim strip go.
- **Biomes and looks.** The whole map draws with the Backcountry's ground shader settings (rock from 105 m, snow painted). The Mountain
  used altitude snow above 205 m: its snow is painted onto its tops instead. The Open Country (drawn as a meadow before) gets the
  Backcountry's natural ground (meadow and forest floor). Frontier's settings were nearly the Backcountry's already.
- **Rivers.** Elk River ran off the west edge; it now ends in a lake (about 130 m across) on the seam with the Open Country. The
  regions' own lakes and rivers come over as they are (the Mountain's lake list goes onto the water grid, as the big map keeps water).
- **Roads.** Three gravel roads, laid with the trails' own cut-and-fill (grade at most 12%), each from a Backcountry place into its
  region: Ranger Station -> Open Country (about 2.6 km), Lakeshore Fuel -> the Mountain's valley loop road (about 2.6 km), the Lost
  Mine's wash -> the Frontier's Cold Creek Lodge (about 1.5 km). Signs at both ends.

## 4. How the old maps' content keeps working

Everything is moved by the region's offset; nothing is re-generated.

| Content | How |
|---|---|
| Land, trails, roads, paint, surfaces, water, flow | copied cell for cell into the core |
| Trees, rocks (with their pieces), logs, bushes, boxes, poles, fixtures, models, signs | moved over (those in the core) |
| Stations (repair) | moved over: the Mountain's 3 and the Frontier's 5 are on the big map's station list |
| Spawn pads and parking | moved over; each region's start is kept (World::regionStarts) for its start point |
| The Mountain's loop road (AI traffic, CRUISE THE LOOP ROAD) | the big map's loop road |
| mountainFeature, frontierFeature, frontierTrack, mountainTrail | unchanged (old map coordinates); `regionToWorld(map, p)` moves them |
| Autopilot places (the DRIVE TO list), jobs | the Backcountry's list plus each region's, moved, named "MOUNTAIN: SUMMIT LOOKOUT" and so on |
| Discovery ("MAP:NAME" lines) | a region's spots keep their old map id and name, so what you found on the Frontier stays found |
| Supply caches (the Frontier's table) | on the big map at their moved spots, keeping MAP_FRONTIER as their map, so opened ones stay opened ("3:ID") |
| Places table (places.h) | `regionAt` names the three regions; the Backcountry's places are unchanged |
| Built-in levels and level files on MOUNTAIN / FRONTIER / OPEN COUNTRY (meadow) | played in the region: `placeInRegion(level)` moves the start, gates, objects, removals, the section canvas, and re-keys the cells onto the big grid. Done when a level is played (both host and guests do it, from the same text), not when it's read, so files stay as they are, the editor edits them on their own map, and older builds still read them |
| Open-country levels of other kinds (desert, forest, canyon, wetland, upland sections) | stay on their own small map: there's no such country on the big map |
| Backcountry level files | unchanged: the grid grows from 6145 to 8193 around the same middle, which the level loader already handles |
| The career branch (`wip/career-spec`): world saves keep a map id | ids are unchanged; a save on map 1, 3 or 5 starts the big map at that region. Positions saved in old map coordinates need `regionToWorld` |

## 5. Multiplayer

The host sends the map id as now; every machine builds the same big map from it (the regions are built from their own generators,
which the guests have too). `net::kVersion` goes from 8 to 9, because a guest on an older build would build the 6 km Backcountry for
the same id. The land hash (World::landHash) is tested to be the same from a fresh build, from the world cache and on 1 or many threads.
A region start is the host's choice for where they start; guests start at the map's start as now.

## 6. Load time, RAM and frame rate

- The regions are built on worker threads while the Backcountry's land is made (Frontier and Mountain from their own world caches when
  warm), then stitched. The whole big map goes into the world cache as one file, so a warm start reads it like today.
- The square is 1.78x the cells, but the part beyond 3072 m outside the regions gets no trees, rocks or bushes, and the drawing streams
  64 m chunks round the camera as now. Expected: about +1 s cold (the regions and the extra land), +0.3 s warm (the bigger cache file),
  +250-350 MB RAM (the bigger grid).
- Targets, against section 1: load (cold and warm) within 1.5x, peak RAM within 1.5x (1.8 GB cold, 1.55 GB warm), frame time the same
  within noise (HIGH, the trailhead), and the frame time at each region's start no worse than that region's old map.
- The dedicated server keeps the whole map: about 1.7x today's (it was 436 MB).

## 7. Tests (new: `onemap/*`)

- `onemap/layout`: the regions are inside the map, don't overlap each other or the Backcountry's places, and `regionToWorld` /
  `worldToRegion` round-trip.
- `onemap/region-copy`: inside each core the big map's heights, surfaces and water are the old map's, cell for cell, and its
  stations, pads and objects are there, moved.
- `onemap/places-dry-reachable`: every region's start, stations, autopilot places and caches, and every Backcountry place, are on dry
  ground, and reachable: a flood fill over 4 m cells (no step steeper than 0.7, no water over 0.6 m) from the Backcountry's trailhead
  gets within 15 m of each.
- `onemap/seams`: along each seam band, no cell-to-cell step over 1.0 (45 degrees) except where the region's own range rises, no
  holes (heights within the map's range, no dry cell below a neighbour's water), and the link roads no steeper than 15%.
- `onemap/old-levels`: a level file on each of MOUNTAIN, FRONTIER and the meadow open country is placed in its region: its start,
  gates, objects and cell edits land where they should, on the same ground relative to them.
- `onemap/cache-roundtrip` and `onemap/hash`: the big map from the cache is the same as built (land hash, every cell), and the land hash
  is the same built twice and on 1 thread or many (the multiplayer check).
- Measurements (section 6) go into `docs/hardware/linux-7900xtx-onemap.md`.

## 8. Risks

1. **Build time and RAM in the tests.** Today's Backcountry tests build the map fresh many times; each build is now bigger.
   Mitigation: the regions build in parallel; if the suite's time grows too much, tests that only need the old Backcountry can turn the
   regions off (`World::stitchRegions = false`, per thread).
2. **The Backcountry's random scatter must not move.** Its new-country cells are seeded by their index from the map's corner, which moves
   when the map grows: the seeds are kept relative to the old 6 km corner, so every tree and rock stays where it was.
3. **A region's old rim on the seam side** was steep: the core stops short of it, and the band carries the core's edge out instead.
4. **The bench suite** measures the Frontier, the Open Country and the Mountain's Boulder Gully as standalone maps, which the hardware
   baselines compare against: it keeps building them that way.

## 9. Phases (each a commit on `wip/onemap`)

1. **The big map.** Region table and coordinates (`src/onemap.{h,cpp}`), the stitched build (regions on workers, seams, the copy, the
   edge ridge opened, the scatter kept), Elk River's mouth lake, the world cache, `regionAt`, the `onemap/*` layout, copy, seams, hash
   and cache tests. Try: `./crumplezone --map 4 --spawn -3300,0,0` (the Open Country side).
2. **Roads and looks.** The three link roads and their signs; the Mountain's painted snow; region starts.
3. **The game.** The map list as start points, `--map 1/3/5` starting the big map at the region, the old settings, autopilot places and
   jobs, discovery keys, the Frontier's caches, the loop road, `net::kVersion` 9.
4. **Levels.** `placeInRegion` when a level is played, the old-levels test.
5. **Measure and report.** Load time, RAM and frame rate (section 6), the full suite, the Windows build, `docs/hardware/linux-7900xtx-onemap.md`.

## 10. Open questions for the owner (none blocking)

1. The name: the big map is still called BACKCOUNTRY (the regions' names show on the M map and in the start points). A new name is a
   one-line change.
2. The north and east beyond the 6 km square are bare edge mountains. Room for a fourth region later (the Freeway stays separate, as asked).

## 11. As built (2026-10-25, `wip/onemap`)

What changed from the plan while building it:
- **Seams by offset.** The band round a region is the Backcountry's own land raised or lowered by how far the region's edge stands
  above it there (smoothed along the edge, more the further out), fading to nothing over 340-380 m (seam side) or 300 m (the other
  sides). The land keeps its own hills through the band, and nothing of the region's is drawn out in straight lines.
- **The edge ridge** is opened within 150 m of each region's square, back to full by 650 m, so a region's sides meet low hills, not a
  wall; the big map has its own edge mountains in its last 330 m all round.
- **Trusted cores:** the seam side as planned (505 / 465 m), and the two sides beside it too (the Mountain 515 m, the Frontier 485 m,
  the Open Country 465 m), where their old rims rose. The far side is the big map's edge: the old rim stays there.
- **The regions' ground** reaches into the band in ragged patches over 160 m (noise), and their trees and bushes just past the core
  (within 40 m) stay, re-grounded, so the woods don't end in a line.
- **Roads:** found with an A* over 8 m cells (gentle grades first, off the foot of cliffs, out of water and of where Elk River and the
  lakes will be, round the regions' cores where it can), cut and filled to 12% with banks that widen with the cut. Ranger Station ->
  Open Country 2.5 km, Lakeshore Fuel -> the Mountain's loop road 2.9 km, the Lost Mine's wash -> Cold Creek Lodge 1.6 km.
- **The 6 km Backcountry kept to the last bit:** its scatter keeps its seeds (counted from the 6 km corner), and `World::height`
  works the cell fraction out from that corner too, so the old map's land under a wheel is bit-identical (the autopilot's Far Side
  run drives the same 202.7 s as on main). The new country's big outcrops stop at 400, counted from the west; a few cells further east
  now get one (under 1% of the rocks within 2.6 km change size, none move).
- Elk River's mouth lake sits in the river's own canyon (its west reach runs 10-25 m below the land, as on the 6 km map).
- The Frontier's frozen tarn and slickrock bowl: the planner finds no way to them on the Frontier itself either, so the reachability
  test leaves them out.
- `--standalone` (dev) builds an old map on its own; the bench suite does that for its scenes.

Measurements: `docs/hardware/linux-7900xtx-onemap.md`.

Not done (later, if wanted): the ground shader's rock/snow heights per region (the whole map uses the Backcountry's; the Mountain's
snow is painted on); the music and ambience by region (the Backcountry's everywhere); a textured M map; region places in the places
table (they're in the DRIVE TO and discovery lists, under their old map ids).
