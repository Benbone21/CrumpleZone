# Procedural infinite world: design

Written 2026-10-25 for PI-TASKS #40. Branch `wip/procworld`. The owner was away, so the decisions below are
the defaults I picked. Each one says what it would take to change it.

**The goal (owner):** "A procedurally generated world engine that we can use any time someone starts a career
mode. It'll just have a fresh brand new map that's different every time and infinitely explorable."

## 0. Summary

- A new map, **PROCEDURAL** (`MAP_PROCEDURAL`, id 6). It is on the map list, takes `--map 6 --seed N`, and
  picking it from the menu rolls a new random seed. The career hook is `ProcWorld::forSeed(uint64_t)`
  (`src/procworld.h`), plus a `map=procedural seed=<u64>` line in a career world save (section 9).
- **The world is a 3 km window that slides with you.** `World` stays what it is today: a finite 1 m grid with
  objects. For this map it covers 3072 x 3072 m of an endless generated land, centred on `World::proc.origin`
  (global metres, a multiple of 64). When you get 512 m from the centre, a new window centred on your 64 m
  chunk is built on a worker thread. That goes through the same pipeline as a map switch (World +
  `Renderer::prepareWorld`, uploaded 3 ms a frame). It is swapped in on one frame, and everything (vehicles,
  ropes, anchors, the camera, effects, you on foot) moves by the exact whole-metre offset. Where the old and
  new windows overlap, the land and objects are identical, so nothing visible changes.
- **This also handles precision.** Local coordinates never go past about 2.1 km from 0, whether you are 100 m
  or 10,000 km from where you started. A global position is `int64 origin + double local`.
- **Everything is a pure function of (seed, global position).** Noise comes from integer hashing. Only `+ - * /`,
  `sqrt` and `floor` are used, never `sin/cos/exp/pow` from libm (`procworld.cpp` has its own polynomial
  versions where a curve is needed). Doubles of integer metres are exact, and `-ffp-contract=off` is on. So
  Linux, Windows and Mac generate the same bytes. A per-chunk hash test pins this down.
- **Infinite by construction:** nothing in the generator needs to know about a neighbouring chunk. Rivers and
  trails come from continuous fields and from a lattice graph whose edges depend only on their two end nodes.
  This is what makes chunk borders seamless (tested).
- The obstacle and growth look stays the house style. Scanned rocks (`World::addRock` picks the scan), the
  code-made trees and bushes, uneven ground (metre-scale bumps everywhere), and the existing ground paints
  and surfaces.

## 1. Earlier attempts reviewed

- `src/wilderness.cpp` in commit `c735d9e` (removed in `f441a6d`) was a seeded **finite** 1.2 km map. It laid
  out a fixed set of features (camp, a mountain with a summit trail, desert basin, lake, mesa, a loop road)
  with `std::cos/sin` and an `Rng` stream. I didn't reuse its code. It can't be made endless (it places
  features by searching the whole map), its random stream depends on order, and its libm calls are not
  bit-identical across platforms. I did reuse its idea of a **fixed set of places, laid out fresh per seed**:
  every lattice node here gets one of a fixed set of place kinds.
- `docs/terrain-generator-handoff.md` / `src/sections.*`: obstacle-section generators for levels. They are not
  used yet. Section 10 has the hook for stamping them along trails later.
- No other procedural-world branch or sibling checkout exists (I searched `git log --all`, the branches and
  `~/Documents/GitHub/MemeNG-*`).

## 2. Biomes and how they blend

Five biomes, all made from existing ground types:

| Biome | Land | Ground (paint / surface) | Growth |
|---|---|---|---|
| MEADOW | broad rolling hills, open valleys | grass, dirt in patches | groves, leafy shrubs, wildflowers, scattered boulders |
| FOREST | steeper wooded hills | grass under trees, mossy rock | dense pine with broadleaf in the low wet ground, logs, ferns, moss rocks |
| BADLANDS | mesas with cliff rims, washes, hardpan | `PAINT_DESERT` dirt, `PAINT_REDROCK` rock on the steep, sand in washes | junipers, sage, yucca, cacti, snags, lots of rocks |
| MOUNTAINS | ridged peaks to ~260 m over the valleys | rock on the steep, scree, snow above the snowline | sparse pine below the treeline, heather, boulder fields |
| DUNES | sand seas with long dune ridges | `PAINT_SAND` / `SURF_SAND`, hardpan between | almost nothing: tumbleweed, a few snags |

- **Climate fields:** two slow noise fields, *wetness* (~4 km features) and *ruggedness* (~3 km), plus a third
  for *heat*, decide the mix. Each biome has a centre in that space. A biome's weight is a smooth polynomial
  bump of its distance from that centre, normalised over the five, so borders are smooth over about 200–400 m.
- **Height** is `Σ w_b · h_b(x, z)` over the biomes plus shared bumps. Ground, growth and rocks are chosen per
  spot by the **dominant** biome, with a dithered blend (a hash decides which of the two leading biomes a cell
  takes, weighted by their shares). So a border is a ragged mix, not a straight line.
- **Seed variety:** the seed shifts and rescales the climate fields and gives each biome a bias (one world is
  mostly forest with a mountain range, another is badlands and dunes). The tests check that different seeds
  give clearly different heights and biome shares.
- The music and ambience biome (`biome.cpp`) already works from the ground and the trees, so it picks this up.

## 3. Rivers across chunk borders

- **Channels are contour lines of a field.** A river runs where a warped low-frequency noise `r(x, z)` crosses
  0.5. The distance to the channel is about `|r - 0.5| / |∇r|`. The gradient comes from finite differences on
  the same pure function, so any machine and any chunk gets the same value at the same point. Because the
  field is defined everywhere, a river always continues into the next chunk, and rivers never end
  mid-country. Two families, a big river field and a creek field at a higher frequency, give rivers and
  creeks.
- **Water that lies flat and flows one way:** the bed and the water level follow a heavily smoothed *drain*
  height (the land low-passed over ~400 m, sampled on the 8 m lattice). The banks are cut down to it. The
  water level is drain height + depth, so the water is level across the channel and varies only slowly along
  it. The current runs along the channel tangent `(∇r)⊥`, signed to go downhill on the drain field. It is
  0.5–1.2 m/s, and faster in riffles.
- **Depth classes** (rivers.h): along the river a 1-D noise picks runs (0.7 m), riffles (0.3 m) and pools
  (1.2 m). Where a trail crosses there is always a **ford**: 0.45 m, gravel bed, slow, marker posts (section
  4). Creeks are 0.25–0.5 m.
- Rivers are weakened where they make no sense. In dunes and badlands they become dry sandy washes (the same
  channel, no water). High in the mountains they narrow to creeks.
- `World::rivers` gets a spot every 8 m along each channel inside the window (for sound and the M map).
  Waterfalls are left for later (section 10).

## 4. Trails and dirt roads

- **A lattice graph.** The plane is cut into 512 m *cells*. Each cell has one *node* at a hashed spot inside
  it (kept 80 m from its edges). Every node links to its east and north neighbours, so the network is
  connected. A diagonal link exists in some cells, and a link is dropped now and then (never one that would
  leave a node with fewer than two). An edge is a function of its two nodes only. Its shape is a Catmull-Rom
  curve through the two nodes and two hashed midpoints, and its grade profile is worked out from the land
  along it. So the two chunks either side of a border compute the same trail.
- **Grading:** the tread follows the land low-passed along the edge (window ±25 m), clamped to a max grade
  (cut and fill), and eased into the land over 8 m. The max grade depends on the edge's rating.
- **Ratings:** a link between two outposts is a **gravel road** (green, max 12%, 4 m tread). Other links are
  dirt trails rated by the steepest land they cross before grading: green ≤ 15%, blue ≤ 22%, black above that
  (max 30%, rocks in the tread). Mountain trails come out black, meadow trails green.
- A trail crossing a river makes a ford. A ford is wide, shallow and gravel, with posts on both banks (the
  river's depth is set to 0.45 m within 14 m of the tread).

## 5. Places in an endless world

- **Every node is a place.** What it is comes from the node's hash and its biome:
  - **Outposts** (`PK_RANGER_STATION`: repair shed, fuel, shop, jobs, save point) sit on a regular sublattice:
    one node in each 3 x 3 block of cells, at a seed-chosen phase. So there is **one outpost every ~1.5 km**
    and never more than ~1.1 km to the nearest one. That gives the career's "range" something to grow into.
  - **Fuel stops** are one in the 3 x 3 block, opposite the outpost.
  - The other nodes, by hash and biome: **cabins** (tow and save, 30%), **old camps** (20%), **lookouts**
    (mountains and ridges: the node is moved to the highest point within 120 m), **mines** (badlands and
    mountains), **ruins**, plus a **cache** within 40–90 m of about every second node, off the trail.
  - **River crossings:** every ford is a `PK_RIVER_CROSSING` place.
  - **Abandoned vehicles:** about 1 node in 25 has one next to it (id and condition from a table), for the
    career to recover.
- **Ids and names:** the id is `PW.<nx>.<nz>.<kind>` (global node coordinates). It is stable for a seed and
  doesn't depend on the window, so saves refer to it. The name comes from word tables indexed by the node
  coordinates modulo coprime table sizes ("COYOTE FLATS OUTPOST", "WILLOW CREEK CABIN"). The id itself is
  unique. Names are unique within about 20 km, which a test checks. Discovery saves `MAP:NAME`, so a repeat
  name 30+ km away would count as found. I accepted that for now; the fix is to key discovery by id
  (section 9).
- `mapPlaces(MAP_PROCEDURAL)` returns the places of the window in play, in window coordinates. `procworld.cpp`
  keeps it up to date on each window swap. So discovery, caches, the autopilot's list and jobs all work
  through the existing code paths unchanged.
- **The start:** a new world starts at the outpost nearest the global origin, on its spawn pad.

## 6. Career progression without a finite map

- **Range:** the nearest discovered outpost, fuel stop or own camp (the career spec as written). Outposts every
  ~1.5 km are within reach of a stock tank, and pushing out means finding the next one.
- **Regions:** the plane is cut into **2 x 2 km regions** (4 x 4 lattice cells). Each has a generated name
  ("REDWATER BASIN") and a finite set of places. `regionAt(MAP_PROCEDURAL, x, z)` names it, and
  `ProcWorld::regionOf(global)` gives its key `R.<rx>.<rz>`.
- **Discovery %:** per region, `found / places in that region` (each region is finite). The career page lists
  the regions you have been in, nearest first, with their %. "Region complete" (90% revealed + every place
  found) pays as the spec says. There is no world-wide %. The world total is shown as counts ("31 PLACES
  FOUND IN 6 REGIONS").
- **Map reveal** (not on main yet): it will be a sparse set of *global* 64 m chunk keys instead of the planned
  96 x 96 bit grid. That is 8 bytes per chunk, run-length encoded in the save.
- **Difficulty with distance:** a soft gradient. Trails get rougher and caches richer with distance from the
  start (`ProcWorld::remoteness(global)` gives 0..1 over 0–8 km), so going far is worth it.

## 7. Floating point far from the origin

- **The window slides, so local stays small.** Physics, rendering and networking only ever see coordinates
  within about ±2.1 km. That is the same as or less than the 6 km Backcountry, which already works.
- **The generator** works on **global integer metres** (`int64`) turned into doubles (exact up to 2^53 m).
  Noise lattice indices are `int64` floor divisions, so there is no `(int)` overflow far out. Sub-metre
  object offsets are added after the lattice lookup.
- Tested at 100 km and at 10,000 km out: the same land shape statistics, and a vehicle drives across several
  rebases and stays stable.

## 8. Multiplayer

- **Same world everywhere:** the welcome message gets an optional trailer: generator version
  `kProcGenVersion` (u16), seed (u64), window origin (2 x i64). A guest with another generator version is
  refused ("THE HOST'S WORLD GENERATOR IS NEWER/OLDER"). An older guest doesn't know map 6 and already turns
  down the welcome. So `net::kVersion` stays 8: the protocol itself didn't change, only a trailer that old
  builds ignore.
- **The host owns the window.** When the group's centre (the middle of the box round every player's vehicle)
  is 512 m from the window centre, the host builds the new window and sends `procshift <ox> <oz>` (a game
  message). Each guest builds the same window and swaps it in. During the swap, vehicle states keep arriving
  in the sender's frame. Every guest and the host apply the offset to remote vehicles when they swap, and a
  state stamped before the swap is shifted on arrival (the message carries the host's clock time of the
  swap; the shared clock already exists). Details in `procstream.cpp`.
- **Spread:** everyone has to be inside the window, so the group can spread up to about 2.5 km. Past that the
  window stops sliding away from the player furthest behind, and the edge shows as the far backdrop. It is a
  limitation of this design, and the doc and README say so. A bigger window (half 2048) would raise it to
  3.5 km at about 30% more memory and build time.
- **Dedicated server:** builds the window at the origin the host would use, and follows `procshift` from the
  first player (the server has no physics of its own). Basic support only.

## 9. Saves

- Anything kept is keyed by **global** position or by a place id. Never by window coordinates.
  - discoveries: `PROCEDURAL:<NAME>` today (as on the other maps); later `PROCEDURAL#<seed>:<ID>`;
  - opened caches: by the cache's id, which is the place id;
  - vehicles left in the world (career phase 1): global `x z` (origin + local), heading;
  - camps (career): global.
- `ProcWorld::toGlobal(const World&, V3 local)` and `toLocal` convert.
- Terrain edits are out of scope: the editor is off on this map.
- **Career hook:** a career world save stores `map=procedural seed=<u64>`. New World picks the seed with
  `ProcWorld::randomSeed()`. Loading the world calls `World::procSeed = seed; World::procOrigin =
  <saved origin or ProcWorld::startOrigin(seed)>` and builds `World(MAP_PROCEDURAL)`. That is all of it (see
  the report).

## 10. Performance

- **Generation:** per 64 m chunk on one core (heights, ground, water, and the object specs). The budget is
  ≤ 6 ms, and the test checks that the median is under the budget. A window rebuild generates only the
  chunks it hasn't got (a cache of the last ~2.5 windows' chunks) on a worker thread, so the game thread
  never generates.
- **Swap:** the swap frame runs `adoptWorld` and the object stages (trees, bushes, rocks and models). I
  measured it and give the number in the report. Vehicles and the camera move by the exact offset in the same
  frame.
- **Frame time:** the window is half the side of the 6 km Backcountry, so the per-frame loops over the map are
  smaller. Target: ≤ the Backcountry's frame time on `--bench`.

## 11. Not in this version (next)

- waterfalls and lakes;
- obstacle sections (`sections.*`) stamped on black trails;
- map reveal (it waits for career phase 6);
- keying discovery by id;
- terrain edits.

## 12. As built (2026-10-25), and what changed from the plan above

Commits on `wip/procworld`, one per phase (a, b+c, d, e), on top of main with the one big map.

- **Files:** `src/procworld.{h,cpp}` (generator, chunk/edge/node caches, places, `World::buildProcedural`),
  `src/procstream.{h,cpp}` (the window sliding in the game), `tests/procworld.cpp`. Small hooks: `world.h/.cpp`
  (map id 6, `World::proc`, the thread-local seed and origin), `main.cpp`, `render.cpp/.h`, `groundshader.cpp`,
  `treedraw.cpp`, `propdraw.cpp/.h`, `forest.cpp`, `places.cpp`, `net.cpp/.h`, `server_main.cpp`, `debuginfo.cpp`,
  `menu.*`, `mainmenudraw.cpp`.
- **Relief (changed after the owner's "too flat"):** `land()` is now a continent swell (+-60 m), mountain belts
  (a warped low-frequency field) holding a domain-warped ridged multifractal (peaks to ~1.1 km), foothills
  (110-280 m of rolling noise) everywhere else, sand basins in the dunes, and in the badlands a plateau cut by
  canyons with 9 m benches. Rivers drain at a level raised 200 m in the ranges and cut valleys as wide as 2x their
  depth (up to 1 km). The valley walls go by the river field's offset over its usual slope (`Macro::riverV`), not by
  the linearised distance, which far out changed many metres a metre and stood up as walls. Every river's valley is
  cut, wet or dry, so a river that fades into a dry wash doesn't leave its walls standing.
- **Trails (changed again):** each edge is routed by A* over a 16 m grid of the carved ground (`groundHeight`) within
  260 m of its nodes. A step costs more the steeper it climbs and far more past the rating's grade (roads 12%, green
  15%, blue 22%, black 32%), so trails zigzag up slopes. The path is smoothed and its height profile is the ground
  low-passed, held to the grade, with fords pinned to the water. Where the grade would leave it more than 18 m above
  or below the land, the grade is relaxed a step at a time instead (a few mountain trails come out steeper than their
  rating). The chunk lays each tread with earthworks as wide as its cut or fill is deep (~1:1.3, at most 40 m). Near
  two legs of a switchback, or two trails, each contributes as near as it is, so there are no steps between them.
  `procworld/no-walls` checks that under 1% of the ground is steeper than 2:1 and that no metre has a cliff over 15 m.
  The tread checks in `rivers-trails` were loosened (kinks 0.6%, steps 2.5 m): trails that cross at different
  heights, or a switchback next to another trail, meet in a short ramp.
- **The backdrop** past the window uses the same carved ground, and the window's own heights on its edge. Before
  this, the uncarved land stood up as a wall at the window's edge.
- **Creeks** only run where the land is within ~10 m of their level. Otherwise they cut gorges through the hills.
- **The slide is invisible:** in a screenshot diff before and after a 64 m slide with nothing else changing, 439 of
  922k pixels differ (float rounding). Getting there took these changes:
  - the shaders' patterns (ground, grass tufts and wind, clouds and their shadows, water ripples, tree bark and
    leaves, prop snow) read `NP = vPos + uNoiseOff`, where `uNoiseOff` is the window origin minus an anchor taken
    when the world was loaded. Lighting and the terrain texels still use the real `vPos`. The anchor moves (one
    pattern change) after 12 km of travel, to keep float precision;
  - the scanned props' picks, trees' and bushes' looks, cliff cards and scatter cells are keyed by world position,
    not by list index or window cell;
  - `World::grassDensity` is keyed by world position too.
- **The swap's cost:**
  - The worker builds the World, the prepared terrain and the scenery meshes (`Renderer::prepareObjects`).
  - The terrain buffers and textures of the window that went out are kept and refilled for the next one
    (`Renderer::Spare`), so no allocation happens. A spare set is made at load: a 2048² RGBA32F `TexImage2D` cost
    ~370 ms on the iGPU.
  - Texels go up through a pixel buffer: a direct `TexSubImage2D` into the big float texture stalled 20-200 ms a
    slice.
  - Index buffers that came out the same are not sent again.
  - The upload is staged: `Renderer::stageWorld` sends a piece per frame (2 ms budget: water, boxes, each tree set,
    the bush and scenery buffers) while the old window is still drawn, and the swap frame only swaps (~0.1 ms). The
    old window is freed on a detached thread. `test_render` "procedural slide" checks that no frame round a slide
    goes over 2x the median + 2 ms. Before any of this the swap frame took 670 ms + 165 ms.
- **Multiplayer (as built):**
  - The host (or the dedicated server, `--map 6 --seed N`) sends `procshift <seed> <x> <z> <due>` (host-only
    chatter) 3 s before `due` on the shared clock.
  - Every machine builds the window meanwhile and swaps at `due`. A late machine waits for its build: a hitch, but
    never out of step.
  - `NetSession::frameFix` moves vehicle states stamped across a slide into the receiver's frame, and `shiftNow`
    moves the buffered ones. Relays go out as sent.
  - Tested: the guest's truck as the host shows it stays within 0.13 m of where it is through a slide.
  - A guest joining in the 3 s before a slide gets it in the welcome and slides too. Pings, buddies on foot and rope
    anchors sent before a slide are moved into the new frame.
  - **Known gaps:** spawn messages are not frame-fixed. The 3 km window follows the host, so a group spread over
    more than ~1.5 km sees the far members drive off its edge (they keep driving on their own window's ground).
- **Saves and career:** `ProcWorld::saveLine(world)` gives "map=procedural seed=N origin=X,Z", and
  `ProcWorld::useSaveLine(line)` sets up the next build. See the report for how New World uses them. Discoveries
  and caches are keyed by seed and place id (`proc::placeKey`: `<seed>#PW.<i>.<j>.<WHAT>`), so a place found in one
  world doesn't count in another.
- **Place names** are unique within 30 km, through an injective fold of the cell index onto the word pairs. Ids are
  unique everywhere.
- **Not done:**
  - waterfalls and lakes;
  - obstacle sections on black trails;
  - map reveal;
  - `World::rivers` spots (river sound comes from the water grid's flood fill);
  - the M map's view centre doesn't follow a slide;
  - reeds on procedural rivers (rivers have gravel beds, not mud).
