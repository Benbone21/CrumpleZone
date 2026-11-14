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
`rosette`, `pads`).

Everything is deterministic: only `+ - * /`, `sqrt` and `detmath.h`'s functions, an integer random generator, no
unordered containers. `procplants/determinism` pins three mesh hashes of a test species; they must be the same on
every compiler and machine. A deliberate change to the generator changes them: update them and say so.

A tree is built 10 units tall (its instance is scaled to the tree's height), a shrub 0.8 in half-width (as the `bush`
model). `lod` 0..3 gives the same individual from fewer triangles: twigs dropped, fewer rings and sides, fewer and
bigger cards and puffs. The budgets are in `tests/procplants.cpp`:

| | full | near copy | far | farthest |
|---|---|---|---|---|
| tree, drawn | to 14 m | to 36 m | to 100 m | beyond (then pictures from 200 m) |
| tree, triangles | 6500 | 2800 | 900 | 320 |
| shrub, drawn | to 11 m | to 30 m | beyond | |
| shrub, triangles | 1100 | 450 | 150 | |

The game builds six seeds of each species it draws (and a snowy set where there is snow), once per map; every tree
is an instance of one of them, turned, scaled and tinted. Past 200 m trees are the existing pictures (impostors),
baked from these meshes like any other tree's.

## How the game uses it

- **A vegetation palette** (`docs/real-terrain.md`) with the line `plants procedural` names species by their file
  names. A tree becomes `World::Tree` kind 4 (`forest::kGenKind`) with `design` = the species' `id`; a shrub becomes a
  `World::Bush` whose kind is `plantgen::kShrubKindBase + id`. Names the generator doesn't know fall back to the
  classic models (reeds, tumbleweed). Moab's palette (`colorado_plateau_desert`) is procedural.
- **Colliders** are as before: `World::plant` stands a `Pole` under every tree, as thick as the generated stem 1 m up
  (`forest::trunkRadius`, from `plantgen::trunkRadius`); foliage and shrubs are nothing to hit.
- **Classic maps** (Frontier, Backcountry, Mountain, the procedural world, levels) are drawn with generated plants by
  biome (below). `--classic-plants` (or `CZ_CLASSIC_PLANTS=1`) restores the classic models exactly. Either way it is a look
  only: the trees and bushes stand where they did, with the classic colliders.
- **The model kit** shows every species as a design of `tree_gen` and `shrub_gen` (the model lab, the proving ground's
  yard, `build/modelsheet tree_gen shrub_gen --set seed=3 --set lod=1`).

## Biomes

Each biome is a palette file in `assets/maps/vegetation/` (format: `src/vegetation.h`). One file serves both kinds of map:

- **A real map** names it (`vegetation hot_desert` in the sidecar) and its `zone` rules grow the species by ground cover,
  slope, elevation and distance to water. Choosing the palette from ecoregion or climate data for a whole-US map would set
  this one key per tile.
- **A classic map** keeps its trees and bushes; `forest.cpp` sorts each into a biome by the ground it stands on (water or
  mud: riparian; sand: hot desert; red rock: high desert; snow: subalpine; on the big maps' plain green country, regions a
  kilometre or so across of montane forest, temperate forest and prairie) and draws it as one of that palette's `stand`
  species: `stand pine fir 5` (a classic pine here is a fir, weight 5), `stand bush creosote 30`, `stand bush:cactus cholla 3`
  (a classic bush of that kind), `stand dead snag 2` with `dead 0.18` (that share of trees drawn dead), `stand deadbush ...`,
  and `dry 0.45` (the share of shrubs in their dry colours). The biome is read a few metres off the plant's own spot, by its
  hash, so two biomes mix over a couple of dozen metres at a border.

| Palette | Trees | Shrubs and ground plants |
|---|---|---|
| `hot_desert` | mesquite, palo verde, catclaw, saguaro, snags | creosote, dry grass, thorn scrub, ocotillo, cholla, barrel cactus, prickly pear, yucca, tumbleweed, dead shrubs |
| `colorado_plateau_desert` (high desert, canyon; Moab) | juniper, pinyon, cottonwood, tamarisk, snags | blackbrush, sagebrush, Mormon tea, saltbush, rabbitbrush, bunchgrass, yucca, prickly pear, dead shrubs |
| `dry_prairie` | juniper, ponderosa, cottonwood in the draws | tall, dry and bunch grass, sagebrush, rabbitbrush, yucca |
| `temperate_forest` | oak, maple, aspen, fir, spruce, snags, leaning snags | leafy shrubs, ferns, tall grass, stumps |
| `riparian_wetland` | cottonwood, aspen, maple, snags | coyote willow, cattails, tall grass, ferns |
| `montane_forest` | ponderosa, fir, spruce, aspen, snags, leaning snags | manzanita, bunchgrass, ferns, stumps, sagebrush |
| `subalpine` | krummholz, spruce, fir, snags | alpine mats, bunchgrass, dry grass |

A stand-in for a classic tree is drawn at that tree's height times the species' `stand_scale` (a mesquite stands 0.55 of the
pine it replaces), with its trunk thickened toward the classic collider. A short desert or treeline tree can't believably
carry a tall pine's trunk: there the classic `Pole` is wider than the drawn stem and stands hidden in the crown.

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
