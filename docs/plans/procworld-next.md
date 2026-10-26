# Procedural world: next pass (plan, for later)

Owner, 2026-10-25, after seeing the mountain screenshots (seeds 1, 2, 123456789). This builds on `wip/procworld` (design doc: `docs/superpowers/specs/2026-10-25-procedural-world-design.md`, section 12 "as built"). Not started.

## What the owner wants
1. **Many more trees in the mountains.** The ranges look bare. Slopes up to the tree line should be properly forested, thinning near the tree line, with open meadows and rock bands breaking it up.
2. **The right trees in the right place.**
   - Pines and other conifers go up in the mountains: dense on the slopes, stunted near the tree line.
   - Broadleaf trees go in the plains, foothills and river valleys: oaks, aspens, cottonwoods along water.
   - **To check first:** as the owner reads it today, it's the other way round. Confirm which tree models each biome picks, and fix the mapping.
3. **Biome borders blend.** No sheer edges. The desert/sand border is the worst: you can see a hard line.
   - The ground colour and texture, the grass and the trees should all change gradually over a wide band (on the order of 200–600 m, wider where the land is flat).
   - The ground should change with the land itself, not along a straight noise contour: sand collects in hollows and washes first, and grass holds on in moist hollows and north-facing slopes.
4. **No colour jumps seen from above.** From high up the colour changes between biomes are "super noticeable … really annoying and distracting".
   - The ground palette should change smoothly across a border, with no step in hue or brightness.
   - Within a biome the colour should vary gently at large scale (macro variation), so borders don't stand out as the only change.
   - The far-view (low-detail) terrain must use the same colours as the near terrain, so there's no colour change at the window edge or at distance either.
5. **More, taller mountains, with snow.** Many more high peaks than now, well above about 1.1 km. Big snow-capped ranges visible from far away, with the snow line varying (lower on north faces, following the land's shape) and snowfields, not a flat cut-off height.

### Added 2026-10-26 (owner, after driving 0.4.7, screenshot from a ridge)
6. **M map: unexplored is fogged.** The procedural map currently shows everything. It should start dark and reveal around where you've driven/walked (wider from high ground), kept per seed. The map is endless: it pans anywhere you've been, with no edge.
7. **Trees all the way to the horizon.** Keep the near tree render distance, but draw far trees cheaply (impostors / billboard clusters / a forest-canopy layer on the far terrain) so distant hills aren't bare land.
8. **The weird lines.** Long pale straight lines run up and across the far hills (roads/trails or their earthworks seen at distance, or terrain LOD seams). Find what they are and make them look like real roads/trails at distance, or remove them.
9. **More features and rocks:** outcrops, boulders, rock bands, scree on steep ground, across all biomes.
10. **Rougher ground** in many sections: bumps, ruts, small dips off the roads, not smooth rolling grass everywhere.
11. **Ground colour patches** (the red/green camouflage blotches in the screenshot) are too strong; covered by item 4.

### Added 2026-10-26 (owner, later the same day)
12. **Snow grows with height:** a light dusting, then patchy, then full snowfields at the top; not a flat cut-off.
13. **Biome variety near spawn:** the owner found only mountains and grass. Desert, swamp/wetland, mesa (red rock buttes and canyons) and the others must appear within a reasonable drive (the first ~5 km), with mountains still a major part. Test: within N km of spawn, on several seeds, each biome appears.
14. **Water:** no rivers seen. Rivers and creeks should be clearly visible and common; add lakes, and waterfalls where rivers drop off steep ground. Check that procedural rivers render water.
15. **Trail clutter:** many more plants, rocks, logs and sticks on and beside roads, gravel and especially trails, things you drive over (small rocks, logs and branches with physics where cheap; tiny sticks visual only). Trails should feel overgrown: bushes crowding in, grass between the ruts, branches over the edges.
16. More rocks and features off the trail, and rougher ground (items 9 and 10).

## Rules: the backbone of this pass (2026-10-26, owner)
The owner: "each biome has to follow a rule on density of vegetation, what things are; structures and roads have to follow certain rules." He drove past a fuel stop that was "just two random gas station sections".

Everything the generator places reads one set of tables, `src/procrules.{h,cpp}`. Tuning the world means editing those tables, not the code that reads them. Tests in `tests/procworld.cpp` check that the world keeps to them.

### A. Biome rules (`kBiomeRules[PB_COUNT]`)
Each biome has one row:
- **Ground:** its base paint and a macro tint (an RGB multiplier for the far view and the map). Its rough-ground amplitude (bumps and small dips, m). How rocky the steep ground gets (rock and scree slope thresholds).
- **Vegetation:**
  - trees per hectare at the foot of its height band;
  - how density changes with slope (none above `treeSlopeMax`), moisture (a multiplier from dry to wet), and height (a tree line from `treeLine` with a ±m wobble, thinning over `treeLineFade` to stunted krummholz);
  - species: the conifer share at low ground and at the tree line (linear in height between), the snag share and the broadleaf share;
  - meadow openings (a noise threshold: open glades with none).
- **Rocks and clutter:** per hectare, for loose rocks, boulders (over 1.4 m), outcrops (clusters of 3–7 big rocks on ridges and steep ground) and logs (fallen trees in the woods). Scree on steep ground above a height.
- **Water:** how common creeks are (scales `creekOn`), and the lake and pond chance in hollows (the wetland's pools).
- **Structures:** which place kinds may be built here (a bit mask).

Biomes:
- meadow, forest, badlands (mesa: red benches, buttes and canyons), mountains and dunes (desert), as now;
- a new **wetland** (swamp): low, wet and flat ground, mud and wet paint, ponds, willows and cottonwoods, snags and reeds.

**Blending:** every layer reads the per-point weights `Macro::w[]`, never a single dominant biome. Trees, rocks and clutter take the weighted mix of the rows' densities and species shares. The ground paint is chosen per cell with a wide, smooth noise threshold (no 1 m salt-and-pepper dither), and the shader blends paints across cells.

**Variety near the start:** the climate fields run at about twice the old frequency, so biomes change every 2–3 km. Each world's biome bias is held to 0.7–1.3. A test checks that every biome appears within 6 km of the start on several seeds. Mountains still cover a large share.

### B. Structure rules (`kStructureRules[]`, templates in `procrules.cpp`)
A structure is a **template**: a fixed list of pieces at offsets in the lot's frame (models, fixtures, posts, signs, the repair bay). A place is built from its template whole or not at all.

Each kind's rule gives:
- its footprint radius (the levelled lot), its clearance (no trees, rocks or bushes within footprint + clearance);
- its slope limit (the land across the lot);
- the road kinds it may stand on: fuel only on gravel (or paved) roads, an outpost at a road junction, the rest on any trail;
- the biomes it may be in (from the biome rows' masks);
- the minimum spacing to another of the same kind.

**Placement:**
- The lot sits **beside** the junction, in the widest gap between the roads and trails that leave it, set back from the treads. It faces back toward the junction, where the road is.
- A short level apron joins the lot to the junction: the approach.
- Nothing stands on a tread.

**Spacing:** a node keeps its candidate kind only if it has the highest priority hash among all nodes within the kind's spacing that drew the same kind. So two kept places of one kind are never closer than the spacing, with no chain of dependencies.

**Kinds:**
- **Outpost (ranger station):** at road junctions. The station building, the repair bay (station), a pump island under its canopy, a woodpile, an outhouse, a picnic table and a map board.
- **Fuel stop:** on gravel roads only, at most one per 3 x 3 cells. A pump island under its canopy parallel to the road, a shop building (a plank cabin) behind it, the gravel lot, a drum rack, a price sign at the road and a map board.
- **Cabin:** in forest, mountains or meadow. A cabin, an outhouse, a woodpile, a fire ring and a table in a clearing.
- **Campground:** in forest, meadow or wetland. Two or three tents round a fire ring, a table and a woodpile.
- **Fire lookout:** on high ground (the node above its surroundings), in forest or mountains. The fire tower, a cairn and a table.
- **Mine site:** in badlands or mountains. The portal, the headframe, ore carts and a woodpile.
- **Ruin:** anywhere. A wrecked pickup, cairns and an oil drum or two.

### C. Road and trail rules (`kRoadRules[]`)
**Kinds**, in order:
- paved (none yet: reserved for a highway);
- gravel road (between outposts, along the outposts' rows and columns of cells);
- trail (green and blue);
- two-track (black: rough, steep and narrow).

A fuel stop is only on a gravel or paved road. Trails and two-tracks join the roads at the nodes.

**Each kind has:**
- its tread half-width and its target grade;
- the steepest grade the routing may let it out to before it accepts a fill;
- its largest cut or fill against the land: 8 m for a gravel road, 2.5 m for a trail, 1.5 m for a two-track;
- its batter (the earthworks' side slope, the steepest part never steeper than 1:1.5);
- its cleared width beside the tread (no trees or big rocks);
- a ditch (a gravel road has a shallow ditch beside the tread);
- its clutter on and beside the tread.

A trail follows the land and climbs steeper rather than ride a causeway. Trails can be steep: that is the point.

**Roadside:**
- **Gravel road:** a cleared verge of 5 m, then the woods. Clutter only past the verge, never on the driving line. A 0.3 m ditch.
- **Trail:** cleared 1 m. Small rocks, sticks and branches on the tread off the wheel tracks, and logs and rocks at the edges. Bushes crowd in to the edge, and grass grows between the ruts.
- **Two-track:** as a trail, rougher: more rocks in the tread, with bigger ones to work round.

**The wall bug (owner screenshot 2):** a trail on a 10 m+ embankment with near-vertical grey sides.
- **The cause:** b4fecdf let a road take 30 m of cut or fill (and a trail 18 m), measured against the smoothed profile rather than the land. A gravel road then rode a 12–16 m fill alongside a switchbacking trail, and the two treads, ~25 m apart in height and a few metres apart across, met in a cliff. Its slope (over 0.8) painted it bare rock: the grey wall.
- **The fix:** cuts and fills are measured against the land and limited per kind (above), and earthworks keep to the batter.

### D. Tests that hold the rules
- Every structure is built from its complete template, and every piece is on its lot.
- Same-kind spacing.
- A structure's front faces its junction, and its lot is clear of every tread.
- Fuel stops only on gravel roads.
- Tree density per hectare in each biome is within its row's band; species follow height.
- No road or trail side is steeper than its batter, and no fill is taller than its kind's limit.
- Every biome appears within 6 km of the start.

## Approach (to refine when started)
- **Biome weights.** Replace the dithered borders with continuous weights from the climate fields (temperature from height and latitude-like noise, moisture from rivers and distance to water, plus slope and aspect). Every layer reads the same weights:
  - ground paint: blended in the shader from a weight texture, as the big map's regions do with `groundLookAt`;
  - grass density and colour;
  - tree species mix and density;
  - rock and scree.
- **Trees:**
  - Choose each tree by species probabilities from the weights: conifer share rising with height and cold, broadleaf share with warmth and moisture.
  - Density follows moisture, slope (no trees above about 35°), and the tree line from temperature. Thin to krummholz near the tree line.
  - **Budget:** mountain forests add a lot of instances, so measure draw cost and use the existing instancing, LOD and occlusion.
- **Colour:**
  - One palette function of the weights, plus large-scale low-frequency variation.
  - The far terrain samples the same function.
  - Check by rendering from 700 m and 2 km and measuring the colour step across each border (no step over a small ΔE).
- **Mountains:**
  - Raise the uplift amplitude and frequency of high ranges: peaks of 1.5–2.5 km, several ranges per 10 km.
  - Keep the valleys, passes and grade-limited switchback trails working.
  - The snow line comes from temperature and aspect, with snowfields in hollows.
  - Check the far view and fog so the ranges read on the horizon.

## Tests to add
- **Species by height:** conifers make up most of the trees above the mountain foot, and broadleaf most in the plains.
- **Tree cover:** mountain slopes below the tree line have at least N trees per hectare.
- **Smooth borders:**
  - the ground colour along a path across each biome border never jumps more than a small amount per metre;
  - no straight-line border segment is longer than X m.
- **Near/far colour:** near and far terrain colour match at the window edge.
- **Peaks:** at least K peaks above 1.5 km per 100 km², each with snow.
- **Frame time:** a frame-time budget in a forested mountain view (real window, 7900 XTX).

## Not in scope
Waterfalls, lakes, obstacle sections on black trails, and map reveal. These are already on the list from the first build.

## Slice: land shape with real drainage (design, 2026-10-26)
Follows docs/plans/procworld-rules.md sections 2 and 3. Status: being built on wip/procworld3-land (see "As built" at the end).

### Why the current rivers can't be patched into this
- Today's rivers are contour lines of a warped noise field (`Macro::riverS/creekS`). Their water sits at a separate smooth level (`drain`), and their valleys are carved down to it afterwards.
- There are no sources and no joining. Checkpoint 1 made them safe: nothing is filled, rings and tight bends fade, and a channel fades where the land isn't 3 m above it.
- But they still cross ridges in gorges. When the ranges were raised to 1.5–2.5 km, those gorges made 14% of the ground steeper than 2:1. The fix for that removed the start windows' water altogether.
- So the land must be shaped around the drainage, not the drainage cut into the land.

### Design: drainage first, then the land round it
1. **The drainage lattice.**
   - One node per D = 160 m cell, jittered within it.
   - Each node has a routing height: the large-scale land only. That is the continent swell, plus a smooth massif along each range belt (highest along the belt's axis), plus a gentle low-frequency tilt. There are no hills and no ridged detail, so there are almost no pits.
2. **Flow.**
   - Each node drains to the neighbour (of 8) with the smallest key (routing height, then hash), but only if that key is smaller than its own.
   - Because the key strictly decreases downstream, the network is a forest of trees: no loops, by construction.
   - A node with no lower neighbour is a sink: a lake basin, with its level at the lowest rim neighbour's height.
3. **Accumulation.**
   - A(n) = 1 + the sum of A over the nodes that drain into n.
   - Memoised in a sharded global cache. A cap of ~30k nodes guards against huge basins.
   - It is a pure function of seed and node, so it is the same on every machine and in every window.
   - **Creek:** A ≥ ~12 (about 0.3 km²).
   - **Big river:** A ≥ ~400 (about 10 km²). Tune for about one big river every 5 km.
4. **Channels.**
   - The node-to-downstream segments, smoothed (Chaikin, together with the main stem upstream and downstream) and meandered by a small perpendicular noise.
   - Width and depth grow with A. Big rivers are 1.5–2 m deep and fast; creeks are 0.3–0.5 m.
5. **Bed level.**
   - level(n) = min(the node's valley floor, the highest upstream level − grade × length).
   - Computed and memoised like A, so it never rises downstream.
6. **The land is built round the channels.**
   - h = level(nearest channel) + valleyProfile(distance, A) + relief × ridgeMask(distance to the nearest channel).
   - The profile is −depth in the channel and rises monotonically. Banks are gentle on soil (about 1:2.5), steeper in rock bands.
   - Relief (the ridged field, the peaks) grows with distance from the channels and with the range belt.
   - Ridgelines therefore fall naturally between drainages. Passes fall where two valley heads meet: the low points of the crest.
   - Peak height 1.5–2.5 km is reached far from the channels, inside the belts.
   - The bed is at or below the banks by construction.
7. **Queries.**
   - A chunk gathers the channel segments of the nodes within the widest valley's reach (about 10 cells, 21² nodes), culled by bounding box.
   - The 4 m macro lattice reads distance, level and A from that list.
   - `groundHeight` (for trail routing and places) uses a thread-local cache of gathered blocks.
8. **Crossings.**
   - A trail edge crossing a creek fords it, as today.
   - Big rivers are given known fords, one every ~2–3 km along each river: a riffle where the bed is raised to about 0.4 m over 20–30 m.
   - Trail A* makes crossing a big river very expensive except at a known ford, or at a bridge cell on main trails (a fraction of them).
   - A bridge is a timber or log deck, made as boxes plus a model, stand-ins like the places'.
9. **Lakes and waterfalls** (a follow-up inside the slice).
   - A sink's basin fills to its spill level; water where the land is below it.
   - Tarns are small sinks high up.
   - Falls go only where the land profile along a channel drops more than ~6 m within ~10 m (rock steps). Each gets a plunge pool (the bed deepened below it) and a falls mesh.
10. **Then:**
    - Raise `terrain.ranges.peak_height` so peaks reach 1.5–2.5 km.
    - Refit `biome.alpine.tree_line` to the new heights. The snow is already at full height.
    - Biome roughness per section 2 goes in the biome rows.

### Tests to add
- **Bed at or below the banks:** every water sample ≤ the ground 30 m to either side, across the flow.
- **Never rising downstream:** along every channel polyline, the level never rises (except into a lake).
- **Tree-shaped:** each node's downstream chain ends in a sink within a bounded number of steps, and no node is visited twice.
- **River spacing:** a 20 km transect crosses about 3–5 big rivers, and every range valley has a creek.
- **No walls:** `procworld/no-walls`, `procworld/earthworks` and `procworld/grades` no worse than today's:
  - main trails: 7.7% of length over 12%, steepest 40%;
  - trails: 1.6% over their rating's limit;
  - earthworks sides: worst 3.9 m a metre (main trails) and 2.9 m a metre (trails).
- **Peaks:** at least one peak over 1.5 km per 100 km², each with snow over 2 km.

### Handover
- Estimated size: about 500 lines for the drainage core (lattice, cache, accumulation, levels, channel query); about 200 for rebuilding `land()`/`macroAt()` round it; about 200 of tests.
- Then tuning: trails, places and biomes all read the land. Every procworld test needs re-measuring.
- It touches only `src/procworld.*` and `src/procrules.*` (new `[water]`/`[terrain]` keys). No render or tree-draw code: no conflict with wip/fartrees.
- Bridges need a deck model or boxes in `buildPlaces`-style code (procworld.cpp).

### As built (wip/procworld3-land)
**Checkpoint 1: the drainage core and the land round it** (generator v4). Where it differs from the design above:
- **Two passes.** Pass 1 is the design's D8 on the routing height R (a slow lowland tilt plus the ranges' massifs, read through a
  ~3 km warp so fall lines bend; no pits from the warp). Its drainage decides the **rivers** (over 1.5 km2) and their levels. Pass 2
  re-routes every other node (creeks and gullies) down the smooth land *with the rivers' valleys cut*, so a creek runs straight down a
  valley's side to its river instead of across it. Rivers and each river's headwater chain keep pass 1. Still a tree: pass-1 edges
  fall in R, pass-2 edges fall in that land, and a pass-2 chain ends in a river or a headwater.
- **Levels.** A river's level is integrated up from its sink, climbing toward its target (R plus a share of the uplift that shrinks
  with its size) at no more than its grade (big rivers 4% in the ranges, a tenth of that in the lowland). A creek's or gully's level
  is a running minimum from its sources down of (the land the rivers left, less its notch depth), never under the river it joins. So
  nothing rises downstream (tested node to node and along every straight piece).
- **The land.** The mass M = R + uplift + rough (crags, knolls, hollows; the desert's benches and dunes). Rivers cut valleys into M
  (a bed, a low bank, a flat floor, then a concave side whose width is the batter times the height it climbs at each point: 1:6 in the
  ranges, 1:14 in the lowland). Creeks and gullies then cut notches into that (deeper where they cross a slope at a slant, so the
  land beside them stands over them both sides; a deep cut through a rise is a steep-sided gorge). Everything is a min of continuous
  pieces, so the land is continuous and the bed is under its banks by construction.
- **Water.** A creek's stretch whose water would stand over the land 30 m across from it is dry there (memoised per node). Water never
  shows over the valley floor round it (where a creek crosses a bigger one's floor on the way in).
- **Trails.** The A* now has a turning cost (state = cell and heading), so climbs are long legs with real switchbacks rather than a
  saw's teeth; the path is corner-cut twice, not three times (which pulled switchback turns straight up the slope).
- **Classes:** gully 0.08 km2 (dry), creek 0.3 (fordable), river 1.5, big river 2.5 km2 (10-26 m wide, 1.5-2 m deep). The rules are
  in `[water]` of assets/procworld/rules.txt.

**Checkpoint 2: fords and bridges.**
- Big rivers' known fords: one node stretch in ~16 (about every 2.5 km along a river) has a ~40 m riffle in its middle, 0.4 m deep.
- Trail A*: a big river's deep water costs 8000 to enter (a known ford: as any crossing, 120); main trails and 15% of the others may
  bridge it instead (350). A trail that may not bridge and found no ford is left out: big rivers are real obstacles.
- A bridge: the samples over the water and ~10 m onto each bank are pinned level at the higher bank or 1.8 m over the water; no
  earthworks under them (the river runs on). Built in each window (buildBridges): a plank deck on log stringers, rails and posts, a
  crib pier under a long one; boxes as stand-ins (no bridge model yet). Each bridge is a river-crossing place ("... BRIDGE").
- Test: procworld/crossings (no trail wades a big river off its fords; decks over the water; the window builds the deck).

**Checkpoint 3: lakes and waterfalls.**
- Lakes: one at each sink of the network (a basin). Its level is filled to 0.25 m under the lowest point of its rim (the land
  round it, without lakes, at 1.45-1.75 times its radius) and at most a metre over where its channels come down. A bowl under the
  water (25-240 m radius, 1.5-8 m deep), a gentle shore rising from it. Water level, still. Trails go round (A* cost 8000).
  Sinks are rare (0-8 in 144 km2), so lakes are too; tarns are the small sinks high up. Swamp ponds: no swamp biome yet.
- Waterfalls: on a stretch that drops 20 m or more (steeper than 1:8), wet, in the ranges, one in eight by the node's dice. Most
  of the drop (up to 14 m) comes at once over a ~4 m rock step; the stretch below has a plunge pool (1.3 m deeper) and a 3:1 rock
  wall behind its start so its valley doesn't wrap round and smear the step. A dry (perched) creek's step is no waterfall. Each
  trail cell's tallest fall is a place ("... FALLS", PK_WATERFALL). **Render hook not done:** the water over a fall is the cells'
  water surface stepping down ~3 m a metre; a falls mesh (sheet and spray) is left for the render side.
- The trails' 16 m search lattice is cached per 1 km tile and shared by every edge (the slowest chunk: 163 ms, from ~400).
- Test: procworld/lakes-falls (rims, level water, the drop at each fall, pools); procworld/river-rules leaves still water to it.

**Checkpoint 4: taller peaks and the tree line.**
- Peaks 1.5-2.5 km: summits in the smooth uplift (so channels run off them rather than cut through them), broad domes in massifs
  here and there on a range's axis (where a ~10 km noise is high), `summit_height` 1800 m times the world's relief. 7 peaks over
  1.5 km in 3 x 144 km2 of seeds 1, 2, 123456789; the highest seen 1.9 km. The ranges' general mass (pass 300, peak 620) is
  unchanged: taller everywhere made every trail steep.
- A trail link that can't hold near its grade (over 20% of a main trail, 12% of a trail, past its limit) isn't laid: the network
  goes round by the valleys and passes.
- Tree line 1550 m (+-80, thinning over 250 m below it), aspens below 900 m; snow as set (dust 1350, patches 1600, whole 2000).
  treeCover() reads the same rules, so the far forest follows (forest/far-field passes).
- Memory: fixed a runaway bridge scan (a trail ending in deep water) that filled RAM; procworld/memory bounds the caches (226 MB
  after a window and three slides).

**After review (owner): taller peaks, more lakes.**
- Summits to ~2.5 km (`summit_height` 2900 m x relief): 18 peaks over 1.5 km in 3 x 144 km2, the highest 2468 m. The owner accepts
  steeper main trails in the ranges; normal trails keep their rating grades (links that can't, or that run mostly on 1.5 m+ cuts
  across steep sides, aren't laid). Earthworks batter 2.5 (was 2.2). Creeks' notches deepen across steeper sides.
- Lakes, one per node at most (lakeOf): basins (sinks), tarns high in the ranges at creek heads and under summits, valley lakes
  behind rock bars on gentle river and creek stretches (the floor overdeepened behind the bar, the outlet over it), ponds in the wet
  lowland. Each filled to just under its rim's lowest point; every one but a basin drains on by its node's channel. ~22 per 144 km2.
- For the render side: ProcWorld::waterFeatures(x0, z0, x1, z1) lists lakes and falls (a fall's lip, level, height, way down,
  width); TrailEdge::bridges lists the bridge spans (ends and deck height).
