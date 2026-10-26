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
