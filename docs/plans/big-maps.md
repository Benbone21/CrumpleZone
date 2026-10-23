# Big maps: a 6 x 6 km Backcountry and Frontier, with rivers, waterfalls and places to find

Plan for PI-TASKS #26 (big maps, points of interest) and #27 (rivers, waterfalls, every model in use).
Status: **approved 2026-10-22 with the owner's answers (section 0).** Phases 0-3 are merged. Phase 4, the 6 km Backcountry land, is on `wip/bigmaps-p4`: drivable, with the world cache packed (35 MB) and the autopilot planning in a window round the route. Measurements are in `docs/hardware/linux-7900xtx-bigmaps.md`. Changes to the plan so far: per-cell scatter seeds are used only for the new country (the old middle keeps its layout); tree impostors are replaced by a distance cut with thinning (1.2 km, 800 m on POTATO); the M map uses coarser cells; the autopilot plans long routes 3 km at a time. The 6 km map's frame time now matches the old 1.2 km Backcountry's, and the server uses 436 MB (`wip/bigmaps-perf`). Next: phase 5 (rivers, waterfall).

Status: **approved 2026-10-22 with the owner's answers (section 0).** Phases 0-3 are merged. Phase 4, the 6 km Backcountry land, is on `wip/bigmaps-p4`: drivable, with the world cache packed (35 MB) and the autopilot planning in a window round the route. Measurements are in `docs/hardware/linux-7900xtx-bigmaps.md`. Changes to the plan so far: per-cell scatter seeds are used only for the new country (the old middle keeps its layout); tree impostors are replaced by a distance cut with thinning (1.2 km, 800 m on POTATO); the M map uses coarser cells; the autopilot plans long routes 3 km at a time. Open: the frame time on the 6 km map is about +26% (CPU loops over the bigger map), and the server uses 560 MB. Next: phase 5 (rivers, waterfall). Phase 5 (rivers and falls) is on `wip/bigmaps-p5`: see its notes under Phase 5.

## Summary (the 10 lines)

1. Backcountry first, then Frontier, grow from 1.2 x 1.2 km to **6 x 6 km** (26x the land). Today's map stays as the middle of the new one, so its trails, caches and tests keep working.
2. The land stays at 1 m detail everywhere (the editor keeps working the same way). The map data takes about 300 MB of RAM instead of 35 MB.
3. The land is built in parallel 128 m tiles and saved to a disk cache after the first build. Target: first launch under 10 s on the desktop (realistic: 5-6 s; a 4-core laptop 15-20 s), later loads under 4 s, map switch with no frame over 50 ms.
4. Only the land near you is drawn in full: chunks within about 600 m stream in and out, and a light 16 m mesh shows the rest of the map. Trees turn into flat cards far away. Grass already only grows near the camera.
5. The frame rate target is today's: Backcountry HIGH averages 5.5 ms (181 fps) on the 7900 XTX, and the big map must stay within +10%.
6. A trail network of about 45-50 km links 8 districts. Each trail has a colour rating, signs and a map board. Obstacle sections (mud pit, rock wall, and the rest) sit along the trails.
7. Places to find: a cabin with a garage and a truck you can keep, 60 caches per map, 7 lookouts, 2 old mines, 6+ river crossings, a ranger station (shop, repair, fuel), 4 fuel stops and 2 waterfalls. The M map starts dark and fills in as you drive (owner's choice).
8. Rivers run across the map with real depth and current. Fords are wadeable, deep pools drown the engine (this ties into #24, the engine repair). A code-made waterfall model scrolls its water, has foam, spray and mist at the base, and roars by size and distance.
9. Nearly all of the 117 models (87 code-made, 30 scanned rocks and cliffs) get a place on the maps. The exceptions are the first-person hand, the editor stand-in, the 4 cm lug nut and the freeway gantry. The table is in section 8. The plan also needs 7 new models: waterfall, garage, ranger station, fuel pump, bridge, mine headframe and gate.
10. Multiplayer: everyone builds the same world from the map id plus a generator version, and a world checksum check stops mismatched builds. Only vehicles, caches, doors and discoveries are synced. Work runs in 11 phases, each on its own branch and measured before and after.

---

## 0. Decisions (owner, 2026-10-22)
| Question | Answer |
|---|---|
| Size | **6 x 6 km** (6144 m square). Targets in section 2 are worked out for that. |
| Order | **Backcountry first**, then Frontier. |
| Today's map | **Kept as the middle district**; re-seeding the random scatter (rocks, trees, bushes move; hand-placed obstacles don't) is OK. |
| M map | **Starts dark and fills in as you drive.** |
| Plunge pools | Float cars away like Frontier's deep water (default). |
| POTATO trees | Drawn to about 800 m on the big maps (default). |
| Cabin truck, career vehicle locking | **Open** (section 9); not needed until phase 7. |

What 6 km costs compared with 4 km: about 2.3x the land data, bake time and cache size. The first-launch target of 10 s holds on the desktop (expected 5-6 s) but not on a 4-core laptop without the cache (15-20 s there); cached loads stay under 4 s everywhere.

## 1. Where we are now (measured 2026-10-22, main @ 898c8e2)

Machine: Ryzen 9 9900X (24 threads), RX 7900 XTX (Mesa radeonsi), 31 GB RAM, Debian 13.
Every run used `nice -n 19`. The physics fingerprint (`bench_sim 12 separated 4000`) is `91850b5879f5e3e3`, unchanged.

**World build (headless, no window).** This is `World(map)` alone, in a scratch tool linked against the game's objects, with the best of 2 runs:

| Map | Size | Grid | Build time | RAM added | Trees | Rocks | Bushes | Logs | Stations |
|---|---|---|---|---|---|---|---|---|---|
| Proving ground | 1.2 km | 1201² @ 1 m | 0.10 s | 24 MB | 650 | 140 | 3000 | 0 | 1 |
| Mountain | 1.2 km | 1201² | 0.33 s | 37 MB | 2400 | 3065 | 3200 | 97 | 3 |
| Frontier | 1.2 km | 1201² | 1.01 s | 46 MB | 2340 | 480 | 1992 | 56 | 5 |
| Backcountry | 1.2 km | 1201² | 0.99 s | 46 MB | 3399 | 900 | 3466 | 200 | 4 |
| Open country | 1.2 km | 1201² | 0.20 s | 26 MB | 650 | 140 | 1700 | 0 | 0 |

**In the game (real window, 1600x900, HIGH, `CZ_LOADPROF=1`):**

| | Frontier | Backcountry |
|---|---|---|
| Map switch, world build on a worker | 1.49 s | 1.08 s |
| Map switch, worst frame while it happens | 75 ms | 53 ms |
| `--bench 600,120`, average | 6.27 ms (160 fps) | 5.52 ms (181 fps) |
| 1% low | 18.3 ms | 16.2 ms |
| Peak RAM, whole game (`--map N --bench`) | 917 MB | 926 MB |
| Launch to the end of the bench (720 frames) | 12.5 s | 10.9 s |
| Dedicated server RAM after startup | 91 MB | 80 MB |

Cold startup on the proving ground (`--load-tour`) breaks down like this:
- world layout: 12.7 s of CPU, spread over 32 world builds (built-in levels and the section park) on workers;
- section park: 4.4 s;
- model generation: 1.0 s;
- car visuals: 1.4 s;
- prop textures: 0.5 s.

**Hard limits found in the code** (these are the reasons for the phases below):
- `World::kHalfSize = 600` is one compile-time size for every map, used about 86 times in 19 files. The shaders also hard-code `600.0`, and the map edge walls are hard-coded at 520 m.
- At 6 km with 1 m cells, as today's code is written, the map would need about 1 GB of world arrays, a 1.5 GB vertex buffer, a 1.2 GB index buffer and a 600 MB terrain texture. **It cannot just be scaled up.**
- Saved levels (`.czl`) store edits by flat cell index on a grid assumed to be 1201 wide (`level.cpp` ~481), so changing the grid breaks every saved level.
- Scatter (rocks, trees, bushes) uses one random stream per map plus rejection loops. Order matters, so it can't be split across threads as written. Some loops check every tree for every attempt.
- Trees at their last level of detail, and all static scenery batches, are drawn out to the 6 km far plane. Only bushes are culled by distance (300 m).
- The coop M map draws one rectangle per 6 m cell every frame (37k now, over 1M at 6 km).
- The prop lookup grids are `vector<vector<int>>` with 4 m cells, which would be 2.4M vectors each at 6 km.
- Tests hard-code 590, 1201 and `kHalfSize-150`.

**What already scales fine:**
- Physics positions are doubles, and network positions are good to ±50 km.
- Terrain contact cost doesn't depend on map size.
- Grass is drawn in rings round the camera.
- Terrain is already in 64 m chunks with 4 LODs and occlusion culling.
- River water and current already live on the height grid, and hydrolock already exists.
- The freeway's `wrapAround` shows how to move the world's origin, though we won't need that below ~10 km.

## 2. Targets

| | Target |
|---|---|
| Map size | Backcountry and Frontier each 6144 x 6144 m (±3072 m, grid 6145²), drivable to about ±2950 m with a natural edge (ridges, a lake, a canyon), not a wall |
| Detail | 1 m terrain cells everywhere, as today |
| First launch (no cache) | ≤ 10 s to driving on the 24-thread desktop (expected 5-6 s); a 4-core laptop 15-20 s (≤ 10 s there is not realistic without the cache) |
| Later launches (cache warm) | ≤ 4 s to driving (expected 3 s: reading and unpacking a ~90 MB cache file is about 1-1.5 s of that) |
| Map switch in game | built in the background, no frame over 50 ms (today 53-75 ms) |
| Frame time, HIGH 1600x900, 7900 XTX | ≤ 6.1 ms Backcountry, ≤ 6.9 ms Frontier (today +10%); 1% low no worse |
| RAM (whole game) | ≤ 1.5 GB (today 0.93 GB) |
| VRAM added | ≤ 450 MB |
| Dedicated server | ≤ 450 MB RAM, startup ≤ 30 s cold and ≤ 5 s cached (the example systemd unit allows 1 GB) |
| Physics fingerprint | unchanged in every phase (the bench runs on the proving ground) |

## 3. The design in plain words

### 3.1 Map size per map
`kHalfSize` stops being one constant. It becomes a field of `World` (`halfSize`, set by each map's builder), and the shaders get it as a uniform. Small maps keep 600 m, so their saved levels stay valid. Level files get a `grid N half H` header line: a missing line means the old 1201/600, so old files still load.

### 3.2 Land data (physics and editor): kept whole, but compact
The physics, the server and the editor need the whole map at all times (a buddy 3 km away still drives on it), so **world data is not streamed; only drawing is.** To keep it small:
- **Heights** stay `float`, 1 m, the whole map: 151 MB. Keeping floats means no change in how cars touch the ground.
- **Surface and paint** stay 1 byte each: 76 MB.
- **Road data** (`roadLat`, `roadAlong`, `roadNear`) and **water** (`waterY`, `flowX`, `flowZ`) are stored only in the 64 m tiles that have a road (9 bytes a cell) or water (12 bytes a cell): roads in about 8% of tiles and water in about 6%, so about 55 MB instead of 800 MB. Lookups go through a tile table, and a tile with no data answers "no road, dry".
- The prop lookup grids become flat arrays (CSR: one index list plus offsets), sized from `halfSize`.

That is about 280 MB, plus about 30 MB of objects (about 85k trees, 25k rocks, 90k bushes).

### 3.3 Building the land: in parallel, then cached
- The analytic height, carving and painting run **per 128 m tile on the job workers** (`src/jobs.*`). Tiles at the edges of trails and rivers carve with a 32 m apron, so seams match.
- **Scatter becomes order-free.** Each 64 m cell gets its own seed from a hash of its position and the map's seed (the `plantRoll` pattern). Spacing checks only look at neighbouring cells, not every tree. Hand-placed things (trail rocks, ledges, POIs) are placed after, from data tables, in fixed order.
- After the first build, the world (heights, packed surface/paint, sparse tiles and object lists) is written to `<configDir>/cache/world-<map>-<genhash>.bin`. It is delta-coded and compressed with the existing texenc-style coder, about 40 MB. The file is keyed by a **generator hash** (the build id plus a hand-bumped `kWorldGenVersion`), so a code change throws it away. This is the same idea as `meshcache`.
- Expected times:
  - today's per-cell cost × 26 = about 26-30 s of CPU, which is ≈ 2.5 s on 24 threads (at 50% parallel efficiency) and ≈ 9 s on 4 cores;
  - loading the cache (~90 MB) is ~1-1.5 s.
- The built-in levels and the section park still bake 32 worlds at startup. Phase 1 makes those lazy: build a level's world only when the level is opened, and the section park only on the proving ground. Otherwise startup grows with the map.

### 3.4 Drawing: stream what's near, a light mesh for what's far

**Terrain:**
- 64 m chunks as now, but **each chunk gets its own small vertex and index buffer, built on a worker** when it comes within ~700 m, and dropped past ~900 m. That is about 450 chunks at ≈ 300 KB each, ≈ 130 MB of VRAM, in a pool of fixed size.
- Upload uses the existing `pumpUpload` budget (3 ms/frame).
- Past that ring, one **far mesh** at 16 m spacing covers the whole map: 385² verts, about 6 MB. It replaces the inside of today's backdrop, and the backdrop moves out to 6 km past the map edge.
- The LOD switch distances (100/220/450 m) stay the same.

**Terrain texture:**
- The RGBA32F texture (height, grass density, paint, water) becomes a **camera-centred window**: 2048 x 2048 texels at 1 m, stored as RGBA16F (32 MB). It updates a strip at a time as you drive (toroidal addressing, so nothing is copied).
- A 4 m texture of the whole map (1536², 19 MB) serves the far mesh and the M map.

**Trees:**
- Instance buffers are built per 256 m cell when the cell enters 1.5 km.
- Past ~600 m a tree is drawn as a **2-quad impostor card** with an atlas baked from the tree model at load.
- Nothing is drawn past 2 km (the fog is ~75% there).

**Rocks, props, buildings:** the existing 64 m batches get a distance cut: 1.2 km for big things (buildings, cliffs, boulders over 2 m), 500 m for small ones. Shadow passes already stop at the 230 m cascade.

**Grass:** no change, already camera-local.

**Water:** the water mesh is built per chunk alongside the terrain (it is per-cell already), so it streams too.

**Fog and draw distance:** unchanged settings. POTATO's 300 m draw distance gets cheaper still.

### 3.5 What the player sees: the new Backcountry (6 x 6 km)
Today's Backcountry becomes **Elk Valley**, the middle district: Trailhead Camp, the Mud Run, Creek Crawl, the mine, the ford and so on, all unchanged. Its edge walls at ±520 m are removed and the land blends outward. Seven new districts surround it (6 km leaves about 2.4 km of new land on each side):

| District | Where | Character | Main places |
|---|---|---|---|
| Elk Valley (today's map) | centre | forest hills, Elk River | everything there now |
| Copper Ridge | north | high rock ridges, snow on top, scree | **Copper Gulch Mine** (headframe, ore carts, tailings, junkyard), 2 lookouts, a fire tower |
| Bear Creek Canyon | east | Elk River's upper canyon | **Bear Falls** (18 m waterfall), a deep pool, a shelf road over the canyon, a log bridge |
| Ranger Flats | west | open meadows, a gravel road | **Ranger Station** (repair, parts shop, fuel, maps), a fuel stop, the main gravel road in |
| Lakeshore | south-west | a large lake, sand, reeds, marsh | **Hollow Pine Cabin with a garage and a truck to take**, a campsite, a boat ramp ford |
| Timber Basin | north-west | logged forest, a working sawmill yard, log decks, muddy skid roads | a second camp with repair, a timber obstacle section, a lookout on a knob |
| High Lakes | north-east | alpine lakes over the treeline, snowfields, granite slabs | a lake chain joined by short creeks, the snowiest trail, a hut with fuel |
| Badlands Wash | south-east | dry wash, sand, slickrock | 2 fuel stops on the way, a wreck site, dunes, the **Little Falls** (5 m) where a creek leaves the canyon |

The new Frontier follows the same pattern later (phase 10): today's map as the middle, with desert to the west, the high range to the north, and the lake and bayou country to the south. That phase gets its own short design pass once Backcountry has shown what works.

### 3.6 Trails
- About **45-50 km** of trails in all (today: about 4 km).
  - A **gravel road loop** (green, about 14 km) links the districts, so anyone can drive to every district.
  - From it, **about 30 trails** of 300 m to 3 km, rated green/blue/black/red as today.
  - Each trail has a sign at each end, a map board at junctions, marker posts and cairns.
- **How they are made:**
  1. A dev tool (`tools/trailroute.cpp`) proposes a route between two places with A* over an 8 m cost grid: slope, water, rock and a rating-dependent grade limit.
  2. The route is smoothed with `splinePoints`.
  3. Its control points are printed as a C++ table.
  4. A person or agent tweaks the table by eye in the game.
- The game itself never routes trails: it carves the fixed tables, so the result is deterministic and identical on the server.
- **Obstacle sections** come from today's generators (`src/sections.*`: STEEP HILL, MUD PIT, ROCKY RIVER, ROCK WALL, DUNES, CLIFF SHELF, TIMBER). They are stamped at chosen spots on the black and red trails, about 10 in all, each with a bypass on green trails.

### 3.7 Points of interest (POIs) and discovery
- **Data:** a new `src/pois.{h,cpp}` holds one table per map. Each entry has an id, name, kind, position (or a named feature plus an offset, like the cache table), radius and, where it applies, a loot entry.
- **Kinds:**
  - `CABIN_GARAGE`, `CACHE`, `LOOKOUT`, `MINE`, `CROSSING`, `RANGER`, `FUEL`, `FALLS`, `WRECK`, `CAMP`.
  - Kinds that come with buildings (cabin, ranger, fuel, mine) call a small builder that places the models plus their collision boxes. The builders follow the `addStation` pattern.
- **Today's code it replaces or extends:**
  - the hard-coded `pilotPlaces` (main.cpp ~546) and `backcountryFeature` lists (the autopilot menu reads the table);
  - the career cache tables (`career.cpp`), which get their spots from POIs of kind CACHE.
- **Discovery:**
  - Today `Coop::discover` marks a place found within 30 m and saves `MAP:NAME`. Keep that.
  - Add **map reveal**: the M map starts dark and lights up in 64 m cells within ~150 m of where you've driven. That is a 96 x 96 bit set per map (1.2 KB), saved with the career and profile.
  - Undiscovered POIs show as "?" on the M map once you are within 400 m, or once a trail sign has pointed to them.
  - Discovering one plays a short sting, shows the place name on screen, and counts toward "places found 12/41" in the pause menu.
- **Caches:**
  - About 50 new ones, so 60 per big map. The deeper in and the harder the trail, the better the loot.
  - The loot lists in `career.cpp` grow to match. Money, parts and tools work as now.
- **The cabin and its truck:**
  - The garage door opens with E.
  - Inside is a vehicle with a dusty, faded paint. Which vehicle is an open question for the owner.
  - In career, driving it out grants the key `VEHICLE:<id>`. The career save already takes free-form keys.
  - Career doesn't lock vehicles today, so this needs one rule: a vehicle with a `VEHICLE:` key in the list is locked until you find it. Owner to approve.
  - In sandbox the truck is just there to take.
- **Ranger station:** repair pad and shop (as the camps have now), fuel, a big map board, a person behind the counter (`person` model), and jobs (`src/jobs.*`) start here.
- **Fuel stops:** a station pad with only `fuel` on (the shop and repair flags off), a pump model and drums.

### 3.8 Rivers
- **A river is data:** a polyline with, at each point, a width, a depth, a flow speed and a bed type (gravel, rock or mud). The builder:
  1. carves the bed and banks (1:2 to 1:4 slopes, steeper in canyons);
  2. writes `waterY` and `flow` into the sparse water tiles, using the same mechanism as Elk River today;
  3. lines the bed with scanned rocks (moss rocks in the forest, nama stones in the badlands).
- **Depth classes on the map's rivers:**
  - riffles at 0.2-0.4 m, where a car crosses at walking pace;
  - fords on trails at 0.4-0.6 m, with a gravel bed and a sign;
  - pools at 0.9-1.5 m, where a snorkel is needed or the engine hydrolocks;
  - the plunge pools under falls at 2-3 m, where you float away downstream like Frontier's deep water today (owner's default, 2026-10-22).
- **Current:**
  - 0.5-1.5 m/s in the open, 2-3 m/s in chutes above falls.
  - Today's buoyancy and drag code (`softbody.cpp` ~340) is kept, with one change: **water level and flow are sampled per node, not once at `nodes[0]`**. Without that, a truck half in a river at its edge gets pushed wrong.
  - This change touches physics, so it gets its own commit, the fingerprint is checked, and the river tests are re-run.
- **Hydrolock:** `hydrolocked` already works. #24 (open the hood, drain, really fix) is the repair side. The plan only makes sure each deep crossing has a recovery anchor (a `ground_anchor` on the far bank) and a cache or shop within reach.
- **Bridges:** the gravel road crosses on a timber bridge, a new model whose deck boxes are its collider. Trails ford.
- **Sound:** today `worldsound.cpp` flood-fills the whole water grid at load. That changes to sound points every 50 m along each river polyline (and lakes as now), and the nearest 3 play.

### 3.9 The waterfall (a code-made model, `src/models/water.cpp`)
- **Model `waterfall`**, with instance params `height` (2-30 m), `width` (2-20 m), `lip` (how far the sheet throws out) and `seed`. Variants:
  - **SHEET**: one smooth curtain;
  - **BROKEN**: a curtain split over rock steps, with 2-3 strands;
  - **CASCADE**: stepped, white over rocks.
- **Pieces:**
  - the falling sheet: a curved ribbon strip from the lip curving down, about 200 triangles, with the new `WATERFALL` material;
  - a foam ring at the base;
  - 6-12 spray billboards;
  - a plunge-pool ripple disc;
  - a mist volume as 3 large soft billboards.
  - The rock lip itself is scanned cliff and rock props placed by the river builder, not part of the model.
- **Animation:**
  - one new material, `WATERFALL`, in `modelkit` and the props shader;
  - its texture scrolls down at a speed from `height`, using the existing time uniform;
  - foam and spray billboards pulse and drift with a cheap shader noise.
  - No particles on the CPU. Budget: under 1500 triangles and 1 extra draw per falls.
- This **edits the model kit and a shader**, which the model brief forbids for model agents. So that part is done by the phase's main agent, and the model itself by a model agent, as the brief describes.
- **Sound:**
  - a roar from `river.wav`, pitched down 0.6 and low-passed;
  - plus a synthesized brown-noise layer, as `audio.cpp` already does for the stream;
  - volume by size (`height × width`) and distance (audible to about 300 m for Bear Falls);
  - reverb-like spread close in.
  - No new sound files are needed.
- **Physics:** the plunge pool is ordinary deep water, and the sheet doesn't push. Optionally later: a cell of strong downward flow under the sheet.

## 4. The level editor on big maps
- **Already works:** sculpt, paint, water, objects and course all work by world position, so they work on a big map once `kHalfSize` is per map.
- **Needed additions:**
  - **Navigation:** an overview map in the editor (Tab) that reuses the M map, plus **click to fly there**. Also a speed boost for high altitude, since the existing flight is too slow to cross 6 km.
  - **Save format:** the `grid N half H` header. The cell maps stay run-length encoded, so a 6 km map with a few edits is still small. Big sculpts could reach the 8 MB network cap, so cell maps also get per-tile compression.
  - **Undo:** the cap of about 3M cell changes stays. One brush stroke is far below that.
  - **POIs:** a POI tool (place, move, rename, set kind). On the built-in maps it writes an overlay into the `.czl` like other edits do today. A dev-only button **"export POIs/trails as C++"** prints the table back for committing, so map design can be done in the editor and saved into the code.
  - **Trails:** a trail tool that drags spline control points. It carves live within a 32 m apron and has the same export button.
  - **Streaming in the editor:** the editor draws through the same streaming path. Terrain edits mark their chunks dirty, so they are rebuilt (the existing `cellEdits` counter, but per chunk).
  - **Big edits on the server:** the server still can't host a level file (`--map` only). Adding `--level FILE` to the server is part of phase 9.

## 5. Multiplayer
- **Same world everywhere:**
  - Today the host sends the map id and every machine builds `World(map)` itself. Keep that, and add **`genVersion` and a world checksum** to the welcome message.
  - The checksum is a 64-bit hash of the height grid at 16 m, the surface tiles and the object counts, computed after the build. That is cheap.
  - On a mismatch, the guest sees "this server runs a different build of BACKCOUNTRY" and is refused, rather than driving through ghost rocks.
  - Windows, Mac and Linux already have different physics fingerprints, so heights can differ by a float ulp across platforms. The checksum therefore rounds heights to 1 cm.
  - If two platforms really disagree, that is a bug to fix in the generator (it uses `std::sin`/`pow` from different libms). Phase 2 tests this on Windows, using a cross-compiled `world_hash` tool under wine.
- **What is synced:**
  - as now: vehicles (with interest rates by distance: 150/400/800 m, fine at 6 km), ropes, lamps, the clock, chat and pings;
  - new, as small TCP game messages like today's `cache MAP ID`: garage door open/closed, the found vehicle taken, place discovered (each player keeps their own discoveries, but a "place found" note goes to everyone).
  - **Not synced:** the map reveal (personal), and trees, rocks and terrain (built from the seed).
- **Server:**
  - It builds the world headlessly as now and uses the disk cache too, so startup is about 2 s after the first.
  - RAM: about 310 MB of world plus objects, under the 450 MB target. It doesn't build any render data.
- **Bandwidth:** players spread over 6 km sit in the 0.5 s band most of the time, so bandwidth goes down, not up. `net/bandwidth`'s "spread" test gets a 6 km case.

## 6. Phases

Each phase is on its own branch from main, merged by the owner. Before and after every phase:
- `--bench-suite`, `--load-tour` with `CZ_LOADPROF=1`, the peak RAM, the tests and the fingerprint;
- all measured on the Linux 7900 XTX with `nice -n 19`;
- the numbers go into the phase's commit message and into `docs/hardware/linux-7900xtx-bigmaps.md`.

**Agent tiers:**
- **Opus:** engine and architecture work, anything touching physics, threading or the save and network formats.
- **Sonnet:** game features in a clear frame.
- **Local Pi:** mechanical data entry and bulk edits against a written list, always reviewed by Sonnet or Opus.

### Phase 0: measuring tools (`wip/bigmaps-0-measure`) [Pi, reviewed by Sonnet]
- **Files:** `tools/worldtime.cpp` (the headless world build timer used for section 1), the Makefile target, and a `--load-tour` option to stop after the maps.
- **Risks:** none.
- **Acceptance:** the tool prints build ms, RAM and object counts per map. `docs/hardware/linux-7900xtx-bigmaps.md` holds the section 1 baseline.

### Phase 1: map size per map, lazy startup (`wip/bigmaps-1-halfsize`) [Opus]
- **Files:**
  - `world.h/.cpp`: `halfSize` member, accessors, CSR prop grids;
  - every `kHalfSize` use: mountain, editcore, frontier, propdraw, walker, render, autopilot, editor, watermesh, worldsound, freeway, scenery, backcountry, coop, and the tests;
  - shaders: the `600.0` literals become a uniform;
  - `level.cpp`: the `grid` header line;
  - `levels_builtin.cpp` and `world.cpp`: lazy level worlds and section park.
- **No map changes size yet.**
- **Risks:**
  - a missed literal (520/540/600) gives a seam;
  - old `.czl` files must still load;
  - lazy building must not add a hitch when a level opens (build it on the worker like a map switch).
- **Acceptance:**
  - all tests pass apart from the known failures, and the fingerprint is unchanged;
  - `levels/*` passes, including the legacy-format test;
  - a new test `world/halfsize` builds a test map at 1024 half-size and checks the edges;
  - cold startup is **at least 3 s faster** (the 32 startup bakes are gone);
  - the bench is unchanged within 3%.

### Phase 2: compact land data, parallel tiled build, world cache (`wip/bigmaps-2-tiles`) [Opus]
- **Files:**
  - `world.*`: packed surface/paint, sparse road and water tiles behind the current accessors;
  - a new `src/worldtiles.{h,cpp}` for the tile table and parallel bake;
  - `src/worldcache.{h,cpp}` for saving and loading;
  - scatter in `world.cpp`, `backcountry.cpp`, `frontier.cpp`, `mountain.cpp` gets per-cell seeds;
  - a `tools/world_hash.cpp`.
- **Risks:**
  - **Re-seeding the scatter moves every tree and rock on every map.** Tests that drive past a rock (creek-crawl, slickrock-ridge, the wall) can change. Trail obstacles placed by hand don't move, only the random scatter. Re-run the frontier/backcountry tests and check each failure by eye.
  - Threads: the bake must give the same bytes on 1 and on 24 threads. Add a test.
  - A stale cache: the cache key must include the build id.
- **Acceptance:**
  - world bytes are identical between 1 thread and N threads, and between a cache load and a fresh build (`world/bake-deterministic`, `world/cache-roundtrip`);
  - RAM per 1.2 km map ≤ today's;
  - a Backcountry build on the 24-thread machine ≤ 0.4 s, and ≤ 0.1 s from the cache;
  - the world hash matches on Windows (wine) and Linux, or the differences are listed;
  - fingerprint unchanged.

### Phase 3: streamed drawing (`wip/bigmaps-3-stream`) [Opus]
- **Files:**
  - `render.cpp`: per-chunk buffers, a worker chunk builder, the pool, the far mesh, the backdrop;
  - `groundshader.cpp` and `groundtex.*`: the camera-window texture;
  - `forest.cpp` and `treedraw.cpp`: per-cell instancing, impostors, the distance cut;
  - `propdraw.cpp` and `scenery.cpp`: the distance cuts;
  - `watermesh.cpp`: per chunk;
  - `coop.cpp`: the M map from the 4 m texture (one textured quad instead of 37k rects).
- **Test map:** a dev-only `MAP_BIGTEST`, today's open country stretched to 6 km, to prove streaming before any real content.
- **Risks:**
  - chunk pop-in (fade over 0.3 s);
  - impostor look at dawn and dusk;
  - upload hitches while driving fast (limit by the pump budget, and build ahead in the direction of travel);
  - the shadow cascade at 230 m must keep its casters;
  - the editor's live terrain edits must reach a streamed chunk.
- **Acceptance:**
  - on `MAP_BIGTEST`, driving a 6 km straight at 120 km/h with `--bench` gives no frame over 25 ms and an average within 10% of Backcountry today;
  - VRAM added ≤ 450 MB (GL memory info);
  - `test_render` and `test_visibility` pass with the chunk pool;
  - Backcountry and Frontier at 1.2 km bench no slower than before.

### Phase 4: Big Backcountry, the land (`wip/bigmaps-4-backcountry-land`) [Opus for the generator, Sonnet for dressing]
- **Files:**
  - `backcountry.cpp`, split into `backcountry_land.cpp` (macro height: ridges, valleys, the six districts) and the existing content;
  - today's map is kept in the middle, blended outward, with no 520 m walls;
  - `biome.cpp`: per-cell lookup, no longer every tree.
- **Risks:**
  - today's backcountry tests drive fixed routes: they must still pass, since their content hasn't moved;
  - the far land must not look empty;
  - the cell-hash scatter density per district needs tuning.
- **Acceptance:**
  - all `backcountry/*` and `career/caches` tests pass (the `|x|<=590` assert becomes `halfSize - 10`);
  - first launch ≤ 10 s and cached ≤ 4 s;
  - the bench within +10%;
  - peak RAM ≤ 1.5 GB;
  - a screenshot tour of each district (`--shot-frames` script) for the owner.

### Phase 5: rivers and the waterfall (`wip/bigmaps-5-rivers`) [Opus for river carving and per-node water physics; Sonnet for the waterfall model and sound]
- **Files:**
  - `src/rivers.{h,cpp}`: the river data and builder;
  - `softbody.cpp`: per-node water sampling, in its own commit;
  - `src/models/water.cpp`: the waterfall;
  - `modelkit.*` and the props shader: the `WATERFALL` material;
  - `worldsound.cpp` and `audio.cpp`: river sound points and the falls roar.
- **Risks:**
  - per-node water changes how cars float, which could change today's ford tests (frontier/ford, backcountry/ford, deep-crossing);
  - the fingerprint must not move: the proving-ground bench has no water under the cars, but check;
  - waterfall overdraw from the spray billboards.
- **Acceptance:**
  - new tests:
    - `rivers/ford-wade`: a 4Runner crosses a 0.5 m ford at walking pace;
    - `rivers/pool-drown`: a Civic hydrolocks in a 1.2 m pool, a snorkelled 4Runner doesn't;
    - `rivers/current-push`: a floating car drifts downstream at about the flow speed;
  - today's water tests still pass;
  - the waterfall sheet in the model lab, each variant under 1500 triangles;
  - in-game screenshots of Bear Falls by day and at night;
  - the falls roar audible at 250 m and silent at 400 m;
  - fingerprint unchanged.

**Phase 5 as built (2026-10-22, `wip/bigmaps-p5`):**
- Water level and current are read at every node of a car (and the intake's level at the intake). Fingerprint unchanged. (A drag that grew with the square of the depth was tried and backed out: it threw the autopilot's Far Side run into 8% damage.)
- `src/rivers.{h,cpp}`: the classes and Elk River's stretches out in the new country: fords (0.5 m, gravel, posts) at x = +-800, 1560, 2800, -1750; pools (1.25 m) at 1300, 2250, -1100, -2100, -2750; riffles (0.3 m) between; runs of 0.8 m elsewhere. `World::rivers` (a spot every 4 m) and `World::falls` go into the world cache (format 4).
- Two falls, both in Bear Creek Canyon: **BEAR FALLS** (18 m, SHEET, x = 1900) and **UPPER FALLS** (6 m, CASCADE, x = 2600), each with a rock chute above, a 2.2-2.7 m plunge pool below and a gorge cut back into the canyon floor. (Not the plan's Little Falls in Badlands Wash: there's no creek there yet.)
- The falls are drawn by the water pass (no extra draw call); their roar is one world voice, the loudest falls from where you are. River sound is still the load-time flood fill.
- Plunge pools behave as Frontier's deep water: a car is held up (not afloat: the buoyancy is under its weight) and a car that can roll is carried off at about the current's speed.
- Not done: bridges, recovery anchors at deep crossings, river sound points every 50 m.
- See it: `./crumplezone --map 4 --eye 1838,34,8,-20,12` (Bear Falls from its pool), `--eye 2565,4,6,-18,6` (Upper Falls), `--spawn 800,4,-90 --eye 784,-24,4,-60,-8` (a truck in the east ford).

### Phase 6: trail network (`wip/bigmaps-6-trails`) [Sonnet; Pi for sign and marker placement from the route tables]
- **Files:** `tools/trailroute.cpp`, the trail tables in `backcountry.cpp` (or `backcountry_trails.cpp`), signs and map boards, and the autopilot place list.
- **Risks:**
  - grade limits: every green trail must be driveable by a 2WD pickup (test it);
  - trails crossing rivers must hit a ford, not a pool.
- **Acceptance:**
  - `backcountry/routes` is extended: the autopilot drives every green trail end to end in a stock Tacoma;
  - a stock Bronco in 4L drives every blue trail;
  - the gravel loop reaches all 6 districts;
  - total trail length is printed (45-50 km).

### Phase 7: points of interest and discovery (`wip/bigmaps-7-pois`) [Sonnet; Opus to review the career and network parts]
- **Files:**
  - `src/pois.{h,cpp}`;
  - `career.*`: caches from POIs, `VEHICLE:` keys, more loot;
  - `coop.cpp`: map reveal, "?" markers, the counter;
  - `main.cpp`: the discovery sting, garage door interaction;
  - `net.cpp`: the new game messages;
  - new models `garage`, `ranger_station`, `fuel_pump`, `mine_headframe`, `gate`, `bridge` in `src/models/buildings.cpp`, made by a model agent per the model brief.
- **Risks:**
  - the career save format: new keys only, so old saves load (test);
  - discoveries in multiplayer;
  - the found vehicle must not duplicate if two players take it (the host decides).
- **Acceptance:**
  - `pois/all-reachable`: every POI is on dry ground within 30 m of a trail, and the autopilot reaches it;
  - `career/found-vehicle`: taking the cabin truck grants its key, and it survives save and load;
  - `career/caches` shows 60 per big map;
  - `net/discovery`: the door and vehicle-taken messages arrive at the guest;
  - the M map reveal saved and loaded;
  - screenshots of each POI.

### Phase 8: obstacle sections along trails (`wip/bigmaps-8-sections`) [Sonnet; Pi to stamp from a placement list]
- **Files:** a placement table in the backcountry content, and `sections.cpp` changed only if a generator needs a position or heading parameter.
- **Risks:** a section's spawn pad and levelling must blend into the trail.
- **Acceptance:**
  - each placed section passes its existing section test at its new place (a test parameterised by placement);
  - each one has a green bypass.

### Phase 9: every model in use, editor, server level hosting (`wip/bigmaps-9-dress`) [Pi for placing from section 8's table; Sonnet for the editor POI/trail tools and the server `--level`]
- **Files:** POI builders (dressing), `editor.cpp` and `editcore.cpp` (overview map, fly-to, POI tool, trail tool, export as C++), and `server_main.cpp` (`--level`).
- **Risks:** small props placed in the hundreds add draw calls, so keep them in the 64 m batches and check the bench.
- **Acceptance:**
  - `models/all-placed`: every id in `mk::catalog()` except `fp_hand`, `menu_vehicle` and `lug_nut` appears in `World::models` of at least one of Backcountry or Frontier, or is drawn as a fixture or tree;
  - the editor can fly across 6 km in under 15 s;
  - POI and trail export round-trips (export, paste, rebuild, the same hash);
  - `--level` on the server.

### Phase 10: Big Frontier (`wip/bigmaps-10-frontier`) [Opus for the land, Sonnet for content, Pi for dressing]
- The same steps as phases 4 to 9 for Frontier, reusing everything.
- Its own short design note first (districts, rivers: Snake River gets a falls coming out of the range).
- **Acceptance:** as for phase 4, plus `frontier/*` and the circuit tests.

## 7. Risks across the whole plan
1. **Re-seeding the scatter moves rocks and trees** that today's tests drive past. Mitigation: hand-placed trail content stays fixed, and only the random scatter moves. Phase 2 re-runs every map test and checks each change by eye.
2. **Cross-platform worlds differ.** Mitigation: the world checksum and the wine check in phase 2. If needed, replace `std::sin/pow` in the generator with the deterministic versions already in `mathx.h` (or add them).
3. **Startup grows with map size.** Mitigation: the disk cache, the parallel bake, the lazy built-in levels.
4. **An empty-looking big world.** Mitigation: districts with their own character, the scanned rocks and cliffs, many small props. Owner screenshot reviews at the end of phases 4, 6 and 7.
5. **Low-end machines** (the Windows RTX 4070 laptop and the M1 Mac phase-2 baselines). Mitigation: the streaming ring and tree distances scale with the quality preset. Re-run the hardware bench suites after phase 3.
6. **Saved levels.** Mitigation: the header line, so old files still mean 1201/600, plus the legacy test.

## 8. Every model and where it goes

Today: 87 code-made models (`src/models/*.cpp`) and 30 scanned props (`assets/props`). "Now" is where each one is placed today. "Big maps" is where it will go. Abbreviations: BC = big Backcountry, FR = big Frontier, POI kinds as in 3.7.

### Buildings and places (`buildings.cpp`, `spawn_pad.cpp`)
| Model | Now | Big maps |
|---|---|---|
| repair_shed | every repair station | every station: 4 camps, Ranger Station, Copper Gulch Mine yard; FR as now plus 2 |
| cabin | BC Trapper's cabin, FR 2, Mountain, menu | Hollow Pine Cabin (with the new `garage`), Ranger Station office, Pass Hut, 3 lookouts' huts, campsites |
| outhouse | menu camp only | every trailhead, campsite and ranger station (about 8 per map) |
| tent | menu camp only | campsites (3 per map), the Lakeshore camp |
| campfire | menu camp only | campsites, lookouts, the cabin |
| woodpile | BC sawmill, menu | sawmill, cabin, campsites, ranger station |
| picnic_table | BC, Mountain, menu | lookouts, campsites, Lakeshore, fuel stops |
| ore_cart | BC old mine | old mine, Copper Gulch Mine (on rails, 4) |
| mine_portal | BC old mine | old mine, Copper Gulch Mine (2 portals) |
| fire_tower | BC fire tower | fire tower, Copper Ridge lookout |
| wreck_pickup | BC wreck site | wreck sites (BC 3, FR 2), a rusting one by the cabin |
| spawn_pad | level starts, section park | map start and parking at Trailhead Camp and the Ranger Station; section starts |
| menu_vehicle | editor stand-in in the menu camp | not placed (it is an editor stand-in) |

### Trail and road (`trail_road.cpp`)
| Model | Now | Big maps |
|---|---|---|
| trail_sign | trail ends (FR, BC, Mountain) | every trail end and junction (about 60 per map) |
| map_board | BC ×5, Mountain | every trailhead, junction and the Ranger Station (about 15) |
| marker_post | trail edges | every trail, edges of shelves and fords, snow poles |
| cairn | BC, Mountain | lookouts, rock trails, the ridge (route markers) |
| race_gate | race and sprint levels | levels only; plus a timed hill climb on Copper Ridge (a built-in level) |
| traffic_cone | editor and model yard only | Ranger Station lot, a washed-out road closure, fuel stops |
| oil_drum | editor and model yard only | fuel stops, mines, sawmill, wreck sites, the cabin garage |
| jersey_barrier | freeway median | closing an old road at the mine, the bridge approaches |
| guardrail | freeway | the gravel road over the canyon, the bridge |
| lamp_post | freeway | Ranger Station lot, fuel stops (lit at night) |
| sign_gantry | freeway | stays freeway only (too big for a back road) |

### Nature and people (`nature_people.cpp`, `plants.cpp`)
| Model | Now | Big maps |
|---|---|---|
| tree_pine | all maps | everywhere by district (impostors far away) |
| tree_broadleaf | all maps | valleys, Lakeshore, river banks |
| tree_snag | FR, BC, Mountain | burnt patch in Copper Ridge, swamp, Badlands |
| stump | FR, BC, Mountain, menu | sawmill, logged areas, cabin, trails |
| bush (all plant kinds) | all maps | everywhere by habitat (reeds on rivers, cacti in Badlands) |
| person | walkers, players | the ranger at the station, a fuel-stop attendant (standing, no AI) |

### Recovery and gear (`gear_recovery.cpp`, `gear_tools.cpp`, `gear_fuel.cpp`, `cargo.cpp`)
| Model | Now | Big maps |
|---|---|---|
| jerry_can | model yard only | fuel stops, the cabin garage, the Ranger shop shelves, beside wrecks |
| traction_boards | model yard only | leaning at trailheads of the sand trails, Ranger shop wall |
| winch_hook | model yard only | Ranger shop, the cabin garage workbench |
| winch | model yard only | Ranger shop, on the found truck's bumper (as a model) |
| shackle | model yard only | Ranger shop, the garage workbench |
| tow_strap | model yard only | Ranger shop, the wreck site |
| tree_saver | winch anchors | as now, plus one left round a tree at the deep crossing |
| ground_anchor | model yard only | a fixed anchor on the far bank of every deep crossing and the falls pool |
| ground_stake, rock_bolt, ice_anchor, chain_sling | winch anchors | as now; a rock_bolt pair at the top of the rock wall section |
| jack | tools on trucks | as now, plus the garage and Ranger shop |
| chain_bundle | stowed on trucks | as now, plus hung at Pass Hut and the snow road start |
| shovel | model yard only | campsites, mines, the snow road, the Mud Run start |
| air_compressor | model yard only | an "air up" post at trail exits (by the map boards) |
| toolbox | model yard only | the garage workbench, Ranger shop, repair sheds |
| fuel_cell | model yard only | fuel stops, the garage |
| light_bar | model yard only | on the wreck pickups, the garage shelf |
| tire_part | repair part | tire stacks at shops, the garage, a tire swing at the cabin |
| bed_gate | pickups | wreck sites (lying in the grass) |
| supply_crate | career caches | every cache (60 per big map) |

### Repair parts and tools (`repair.cpp`, `parts.cpp`, `frames.cpp`)
| Model | Now | Big maps |
|---|---|---|
| lug_wrench, wrench, hammer, ratchet, pry_bar, tire_iron | tools in hand, trunk, shop | as now, plus on workbenches in the garage, repair sheds and Ranger shop |
| pliers, screwdriver | model yard only | the same workbenches |
| drive_shaft, tie_rod, control_arm, cv_axle, spark_plug | car parts, spares | as now, plus Ranger shop shelves and the mine junkyard |
| torn_end | broken-part caps | as now (on cars only) |
| crate_engine_v8 / flat4 / v6 / i4 | in cars | on pallets in the garage and Ranger shop; a v8 in the mine junkyard |
| gearbox_manual, gearbox_auto, transfer_case, differential, coilover, leaf_pack | in cars | shop shelves, junkyard, the wreck site |
| frame_rail, frame_cross, frame_hanger, frame_shackle, frame_tower, frame_stay, frame_bodymount, frame_hook, frame_skid, frame_jackpad | car frames | as now, plus a rusted frame pile in the mine junkyard and the wreck site |
| lug_nut | unused | not placed (4 cm, unseen; it is used by the wheel jobs' tests only) |
| fp_hand | first-person only | not placed (by design) |

### Scanned props (`assets/props`)
| Prop | Now | Big maps |
|---|---|---|
| rock_moss1_a-f, rock_moss2_a-g (13) | scatter on all maps, chosen by fit | forest districts, river beds and banks in Elk Valley and Bear Creek Canyon |
| rock_07, rock_09, boulder_01, stone_01 | scatter | everywhere, boulder fields on Copper Ridge, trail-side rocks |
| nama_boulder_03-05, nama_boulders_01a/b, nama_stone_a-e (10) | scatter | Badlands Wash, dry creek beds, the desert in big Frontier |
| cliff_face_01, cliff_face_02, cliff_nama_01 | steep ground on every map, ≤900 | canyon walls in Bear Creek, the falls' rock lip, the ridge; the cap raised and placed per streamed cell |

To steer which rock goes where, the scatter picks a rock set by district (moss in the forest, nama in the badlands) rather than only by proportion.

### New models this plan needs (by a model agent, per `docs/model-brief.md`)
| Model | File | Where |
|---|---|---|
| waterfall (SHEET, BROKEN, CASCADE) | `src/models/water.cpp` | Bear Falls, Little Falls, FR's Snake River falls |
| garage (with a door that opens, instance param `open`) | `buildings.cpp` | Hollow Pine Cabin |
| ranger_station | `buildings.cpp` | Ranger Station |
| fuel_pump | `buildings.cpp` | 4 fuel stops, Ranger Station |
| bridge (timber, `length`, `width`) | `trail_road.cpp` | the gravel road's river crossings |
| mine_headframe | `buildings.cpp` | Copper Gulch Mine |
| gate (a swing gate on posts) | `trail_road.cpp` | trail closures, the cabin drive |

## 9. Open items
1. **The cabin's truck:** which vehicle, and whether career should lock vehicles until found. Not needed until phase 7. The cabin and garage get built either way.
2. Everything else was answered: see section 0.
