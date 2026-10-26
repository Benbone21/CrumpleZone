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
