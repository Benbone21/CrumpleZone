# Procedural plants

Trees, shrubs, grasses, yuccas and cacti grown by rules from a species description, so a new species is a text file and
not a hand-written model. The code is `src/plantgen.h/.cpp` (the generator), `src/forest.cpp` (where the game draws
them) and `src/models/procplants.cpp` (the model kit's view of them). The species are `assets/plants/*.txt`.

## What it does

`plantgen::generate(species, {seed, lod, snow, dry, season}, mesh)` grows one individual:

1. **A skeleton.** Stems rise from the foot; each branch bends as it grows (`gnarl`, `up`), carries children at an
   angle (`angle`, `count`, `start`), and those carry theirs, `orders` deep. The limbs' lengths up the stem follow
   the `crown` shape. Some limbs are dead (`dead`, `dead_low`, `dead_top`), some snapped (`broken`).
2. **Fitted.** The skeleton is scaled to a height of 1 (and its crown to `width`), then thicknesses come from the pipe
   model: a branch's radius^`pipe` is the sum of its children's, normalised so the stem's foot is `trunk_radius`.
3. **Wood.** A tube round every branch, more sides on thick ones, twigs as three-sided spikes, snapped ends capped.
4. **Foliage.** Clumps along the leafy branches (`leaf_order`, `leaf_from`, `leaf_step`): a solid lumpy `puff` for
   mass and `cards` leaf cards round it. Each corner's normal leans from the clump's middle and from the crown's
   middle, and the colour darkens toward the inside (`ao`), so a crown shades as a volume. The tree shader cuts the
   cards' pixels to a leafy outline (`cut`, material `mk::LEAFCUT`).
5. **Snow** on what faces up; **dry** and **season** move the leaf colours.

Bunchgrass, rosettes (yucca) and cactus pads have their own small growers in the same file (`form bunchgrass`,
`rosette`, `pads`). A fern is a bunchgrass whose blades are fronds (`pinnae`); a ribbed cactus is a woody plant with green
stems and `ribs` (a barrel cactus also `barrel 1`); a fallen log is a single dead stem with `lying 1`.

The far copies of a tree (30 m and further) are a few big solid tufts in place of its clumps: each drawn in toward the crown's
middle by as much as it was grown, evened out and shaded by the crown's shape, so the far tree is the full one's size and
reads as one mass. How much they cover is also what their shadows cost to fill (`kGrowPuff`).

Everything is deterministic: only `+ - * /`, `sqrt` and `detmath.h`'s functions, an integer random generator, no
unordered containers. `procplants/determinism` pins three mesh hashes of a test species; they must be the same on
every compiler and machine. A deliberate change to the generator changes them: update them and say so.

A tree is built 10 units tall (its instance is scaled to the tree's height), a shrub 0.8 in half-width (as the `bush`
model). `lod` 0..3 gives the same individual from fewer triangles: twigs dropped, fewer rings and sides, fewer and
bigger cards and puffs. The budgets are in `tests/procplants.cpp`:

| | full | near copy | far | farthest |
|---|---|---|---|---|
| tree, drawn | to 11 m | to 30 m | to 100 m | beyond (then pictures from 200 m) |
| tree, triangles | 6500 | 2800 | 480 | 150 |
| shrub, drawn | to 8 m | to 22 m | beyond | |
| shrub, triangles | 1100 | 450 | 150 | |

The game builds six seeds of each species it draws (and a snowy set where there is snow), once per map; every tree
is an instance of one of them, turned, scaled and tinted. Past 200 m trees are the existing pictures (impostors),
baked from these meshes like any other tree's.

## How the game uses it

- **A vegetation palette** (`docs/real-terrain.md`) with the line `plants procedural` names species by their file
  names. A tree becomes `World::Tree` kind 4 (`forest::kGenKind`) with `design` = the species' `id`; a shrub becomes a
  `World::Bush` whose kind is `plantgen::kShrubKindBase + id`. Names the generator doesn't know fall back to the
  classic models (reeds, tumbleweed). Moab's palette (`colorado_plateau_desert`) is procedural.
- **Colliders**: `World::plant` stands a `Pole` under every tree, as thick as its stem 1 m up (`forest::trunkRadius`, from
  `plantgen::trunkRadius`) and 0.6 of the tree tall; foliage and shrubs are nothing to hit.
- **Classic maps** (Frontier, Backcountry, Mountain, the procedural world, levels) still plant classic kinds (pine, broadleaf,
  snag), but `World::plant` settles what each grows as: a species of the plant biome it stands in (below), stored in
  `World::Tree::species`. The tree is that species in the world: its height (a mesquite stands 0.55 of the pine it was planted
  as, `stand_scale`), its crown and its `Pole` (the species' own stem). `--classic-plants` (or `CZ_CLASSIC_PLANTS=1`) is a look
  only: it draws the classic pine, broadleaf and snag models on those same trees, at their heights, so a classic trunk is drawn
  thicker or thinner than the `Pole` under it (a classic pine on a mesquite's thin stem; a classic bush look for the shrubs).
  The colliders never depend on the switch.
- **The model kit** shows every species as a design of `tree_gen` and `shrub_gen` (the model lab, the proving ground's
  yard, `build/modelsheet tree_gen shrub_gen --set seed=3 --set lod=1`).

## Biomes

Each biome is a palette file in `assets/maps/vegetation/` (format: `src/vegetation.h`). One file serves both kinds of map:

- **A real map** names it (`vegetation hot_desert` in the sidecar) and its `zone` rules grow the species by ground cover,
  slope, elevation and distance to water. Choosing the palette from ecoregion or climate data for a whole-US map would set
  this one key per tile.
- **A classic map** plants classic trees and bushes; each grows as one of the `stand` species of the biome it stands in:
  `stand pine fir 5` (a classic pine here is a fir, weight 5), `stand bush creosote 30`, `stand bush:cactus cholla 3`
  (a classic bush of that kind), `stand dead snag 2` with `dead 0.18` (that share of trees dead), `stand deadbush ...`,
  and `dry 0.45` (the share of shrubs in their dry colours).

### Where a biome is (classic maps)

The biome areas are marked by the map's builder, in `World::plantZones`: a grid over the map, 16 m a cell, one `PlantZone`
(`world.h`) each. It is marked before anything is planted and saved with the map (the world cache). `forest::plantBiomeAt`
reads, in this order:

1. **The ground's own say**: in water or within 9 m of it, or on mud: `riparian_wetland`; on snow or ice: `subalpine`.
2. **The builder's area** (`World::plantZone`).
3. Where nothing is marked (the proving ground, a blank map): red rock paint `colorado_plateau_desert`, sand or desert paint
   `hot_desert`, else `temperate_forest`.

It is read up to 13 m off the plant's own spot, by the plant's hash, so two biomes mix over a couple of dozen metres at a
border. The hash comes from where the plant stands in the world (`forest::plantHash`), not from its place in a list.

| Map | What the builder marks |
|---|---|
| Wilds (levels) | the whole map by its kind: meadow `temperate_forest`, desert `hot_desert`, forest `montane_forest`, canyon `colorado_plateau_desert`, wetland `riparian_wetland`, upland `subalpine` (`World::buildWilds`) |
| An obstacle section | its own ground (the trail, run-in and run-out): dunes `hot_desert`, rock wall and cliff shelf `colorado_plateau_desert`, timber `montane_forest`, mud pit `riparian_wetland`, steep hill `subalpine`; the rocky river keeps the country it lies in (`buildSection`) |
| Mountain | by height: valley floor `temperate_forest`, from 45 m `montane_forest`, from 150 m `subalpine` |
| Frontier | the west desert `hot_desert`, Blackwater Bayou `riparian_wetland`, Cold Creek `subalpine`, the northern range `montane_forest` (`subalpine` over 105 m), the rest `temperate_forest` |
| Backcountry | Elk Valley `temperate_forest` (its north side `montane_forest`, `subalpine` up by the snow), the dune bowl `hot_desert`; the districts: Badlands Wash `colorado_plateau_desert`, Ranger Flats `dry_prairie`, the northern districts and Bear Creek Canyon `montane_forest` (and anything over 80 m; `subalpine` over 175 m), the Lakeshore and the south uplands `temperate_forest` |
| The big map's regions | each old map's core brings its own areas and its trees as they grew there (`stitchRegionContent`); the grown country: the Mountain's `montane_forest` / `subalpine`, the Frontier's badlands `colorado_plateau_desert` and its cold bench `montane_forest` / `subalpine`, the Open Country's southern canyons `colorado_plateau_desert`, its northern hills `temperate_forest`, its meadows `dry_prairie`; the edge mountains `montane_forest` / `subalpine` |
| The procedural world | each chunk marks its sixteen 16 m squares from the climate's biome with the most weight there (`Chunk::zone`): mesa country (badlands weight over 0.15) `colorado_plateau_desert`, dunes `hot_desert`, a meadow no wetter than the plains `dry_prairie` and a wetter one `temperate_forest`, forest `temperate_forest` (toward the ranges `montane_forest`), the mountains `montane_forest` and within the thinning band under the tree line `subalpine` |
| A real map with a classic palette | the whole map, where its palette is one of the seven |

In the procedural world a plant reads only its own 64 m chunk's cells, so a tree is the same species with the same trunk in
every window that holds it (`procplants/procedural-world`).

### What is settled when, and who must agree

- **Trees: when planted.** `World::plant` calls `forest::settleSpecies`: the biome there, a species from its `stand` list by the
  tree's `plantHash`, then the tree's height and crown times that species' `stand_scale`, and the `Pole` from its stem. A tree
  added later (a level's objects, the editor) is settled the same way from the ground as it is then.
- **Bushes: when the forest is built** (`forest::buildBushes`): nothing to hit, so `--classic-plants` can leave them classic.
- The species is part of the world: `World::landHash` takes it in, `worldprint` hashes it with the trees (and the areas), the
  world cache stores both and is keyed by `forest::standHash()` (the seven palettes' text and every species file's), and a
  multiplayer host sends that hash in its welcome: a guest with other plant files is refused (their trunks would differ).
  Changing a palette's `stand` lines or a species' stem or `stand_scale` changes the worlds: it is a new world version.

| Palette | Trees | Shrubs and ground plants |
|---|---|---|
| `hot_desert` | mesquite, palo verde, catclaw, saguaro, snags | creosote, dry grass, thorn scrub, ocotillo, cholla, barrel cactus, prickly pear, yucca, tumbleweed, dead shrubs |
| `colorado_plateau_desert` (high desert, canyon; Moab) | juniper, pinyon, cottonwood, tamarisk, snags | blackbrush, sagebrush, Mormon tea, saltbush, rabbitbrush, bunchgrass, yucca, prickly pear, dead shrubs |
| `dry_prairie` | juniper, ponderosa, cottonwood in the draws | tall, dry and bunch grass, sagebrush, rabbitbrush, yucca |
| `temperate_forest` | oak, maple, aspen, fir, spruce, snags, leaning snags | leafy shrubs, ferns, tall grass, stumps, fallen logs |
| `riparian_wetland` | cottonwood, aspen, maple, snags | coyote willow, cattails, tall grass, ferns, fallen logs |
| `montane_forest` | ponderosa, fir, spruce, aspen, snags, leaning snags | manzanita, bunchgrass, ferns, stumps, fallen logs, sagebrush |
| `subalpine` | krummholz, spruce, fir, snags | alpine mats, bunchgrass, dry grass |

The layout of the maps is as it was: only what grows changed. So a biome has trees only where the map already planted some
(the Frontier's desert has a few dead snags, the procedural dunes almost no trees: their desert shows in the shrubs and cacti).

## Adding a species

1. Write `assets/plants/NAME.txt` and add `NAME` to `assets/plants/index.txt`.
2. Give it `class tree` or `class shrub` and an `id` no other species of its class has. **Never change an id** once a
   map uses it: it is what worlds and caches store.
3. Pick the `form` nearest its growth habit. A form is only a preset of the keys below; override what differs.

   | form | habit | examples |
   |---|---|---|
   | `excurrent` | one straight leader, limbs in tiers | fir, spruce, ponderosa, pinyon, aspen, snags |
   | `decurrent` | a trunk that forks into spreading limbs | juniper, cottonwood, oak |
   | `multistem` | several stems from one foot | tamarisk |
   | `shrub` | many short stems, a dome | sagebrush, blackbrush, saltbush, rabbitbrush, Mormon tea |
   | `wand` | a thicket of thin upright stems | coyote willow |
   | `bunchgrass` | a tuft of blades and seed stalks | ricegrass |
   | `rosette` | sword leaves from a crown, a flower stalk | yucca |
   | `pads` | flat pads growing off one another | prickly pear |

4. Set its colours (`bark`, `bark2`, `leaf`, `leaf2`, `leaf_dry`, `autumn`, `flower`). The game's light is strong:
   colours that look right are duller and darker than a photo's.
5. Look at it: `make build/modelsheet && build/modelsheet --out /tmp/sheet --size 480 --set seed=2 tree_gen`, several
   seeds and `--set lod=1..3`. (The sheet draws leaf cards whole; the game cuts them.) Then in the game: name it in
   a palette, or give it a `replaces` line and run with `--proc-plants`.
6. Run `build/run_tests procplants/`: the file must parse, every far copy must be within budget and lighter than the
   one before, a tree must have wood at its foot.
7. To grow it on a real map, add it to a palette file in `assets/maps/vegetation/` (a new region is a new palette
   file: lists of species per zone with densities; nothing in the code names a region).

Lengths are fractions of the plant's height, angles degrees, colours `r g b`. "Per order" keys take one value per
branching order (0 the stems, 1 the limbs, ...); past the last given, the last is used. `plantgen::keys()` is the
full list with defaults; the main ones:

| key | meaning |
|---|---|
| `size A B` | how big it grows (m): a tree's height, a shrub's half-width |
| `stems A B`, `stem_spread`, `stem_base` | how many stems, how far they lean out, the ring they rise from |
| `trunk_len`, `trunk_radius`, `flare`, `pipe`, `taper`, `twig` | the stem's length before fitting, its radius at the foot, the foot's flare, the pipe exponent, extra taper, the thinnest radius |
| `orders` | branching depth (1: bare stems) |
| `segs`, `gnarl`, `up` (per order) | bends along a branch, how crooked, how it turns up (+) or droops (-) |
| `start`, `count`, `angle`, `planar` (per parent order) | where children begin, how many, at what angle, kept to the sides (flat boughs) |
| `angle_top`, `angle_var` | the limbs' angle at the top of the stem; variation |
| `length` (per parent order), `falloff`, `crown` | limb length x the crown shape (`conical`, `spire`, `round`, `columnar`, `spreading`, `flat`); twig length x the parent's |
| `dead`, `dead_low`, `dead_top`, `broken` | dead limbs, dead lower crown, a dead spike top, snapped branches |
| `width` | the crown's radius over the height the plant is fitted to (also how far a palette keeps it off a trail) |
| `leaf_order`, `leaf_from`, `leaf_step`, `leader` | which branches carry foliage, from how far along, how often, and the top of the stem |
| `cards`, `card L W`, `scatter`, `align`, `flat`, `droop`, `cut` | leaf cards per clump, their size, spread, direction, and whether the shader cuts them |
| `puff R TALL` | a solid tuft under each clump (0: cards only) |
| `ao`, `leaf_tip`, `far` | darker inside the crown, lighter tips, how much the far copies' cards grow |
| `green_stems 1` | the branches are the foliage (Mormon tea) |
| `blades`, `blade L W`, `spread`, `stalks A B`, `stalk_len`, `skirt` | grass and rosettes |
| `pads A B`, `pad L W T` | cactus pads |
| `pinnae N` | bunchgrass: each blade a frond with N pairs of leaflets (the full model only) |
| `ribs N DEPTH`, `barrel 1` | a cactus: ribs up the green stems (ridges toward `deadwood`, the spines); the stem swollen to a dome with `flower` heads on top |
| `lying 1` | the single stem lies along the ground (a fallen log; `width` is half its length) |
| `replaces KIND N` | the classic model it stands in for with `--proc-plants`: `pine 0..2`, `broadleaf 0..2`, `snag 0..2`, `dryland 0..3`, `bush NAME` |

## Species lists per ecoregion

A region is a palette file (`assets/maps/vegetation/REGION.txt`): zones by ground cover, slope, elevation and distance
to water, each with trees and shrubs a hectare and a weighted species list. To cover another part of the US, add the
species files it needs and a palette that names them; the real-terrain sidecar picks the palette with
`vegetation REGION`.

## Rocks and buildings (not built)

The same shape would serve other generators: a data folder with an index (`assets/rocks/`), a `generate(spec, seed,
lod, mesh)` that uses only deterministic maths, stable ids in the data, a `mk::LateReg` so the model kit shows every
entry (models made from data files can't register while statics are set up), and a look that names the entry so the
instanced drawing (`forest::Shape`, or the scanned props' instances for solid things) builds each variant once.
Solid things need their collider made from the same description (as `plantgen::trunkRadius` does for a trunk), on
the world-building side, never from the drawn mesh.
