# Biome vegetation: review (branch wip/plant-biomes)

Seven palettes in `assets/maps/vegetation/`, 45 species in `assets/plants/`. How it works: `docs/procedural-plants.md`, "Biomes".
Shots: Backcountry, 10:00, `--photo --adaptive 0`, 1600x900, offscreen, frame 240. Each `BIOME.jpg` is four views of one spot:
top a wide view from 24 m up, bottom from a driver's eye; left `--classic-plants`, right the default (generated, by biome).
The sheets (`sheet-PALETTE.jpg`) show every species a palette draws, seed 2 (leaf cards uncut, as the sheet tool draws them).

| File | Spot (x,z) | Verdict |
|---|---|---|
| `temperate_forest.jpg` | -54,-250 | Mixed oak, maple, aspen and fir with real trunks; from above a varied canopy instead of cones and balls. Far trees are lumpy solids much like the classic style; the change at 36 m is visible if looked for. |
| `montane_forest.jpg`, `riparian_wetland.jpg` | -2000,1250 and -2061,1290 | The same dense wood by a river: orange ponderosa trunks, firs, white aspens, leaning snags; cattails and willow at the bank. The best of the set. The "riparian" spot is mostly montane forest: only plants within a few metres of water change. |
| `subalpine.jpg` | 1921,-3184 | Snow-laden stunted firs and spruce with dead leaning snags: reads as treeline. Krummholz are drawn 0.35 of the classic tree's height, so the classic colliders stand taller than the trees (hidden in them). |
| `hot_desert.jpg` | 711,-897 | Not a desert: the only sand with trees on Backcountry is a river beach. A few mesquite and dead trees at the sand's edge, forest behind. The desert palette is shown fairly only by its sheet. |
| `dry_prairie.jpg` | -1842,2460 | The drive-height camera stands inside a crown; the wide view shows juniper, ponderosa and cottonwood over grass tufts where there was forest. A prairie with a forest's tree count is not a prairie: the layout is unchanged, so only the species change. |
| `high_desert_moab-bench.jpg`, `high_desert_moab-river.jpg` | Moab 6300,800 and 4300,4420 | The high desert on the one map made for it: junipers (now single-trunked, fuller), blackbrush, dead shrubs and dry grass on the bench; cottonwood, tamarisk and willow by the river. |
| `sheet-hot_desert.jpg` | | Saguaro, ocotillo, mesquite, palo verde and creosote are recognisable. The barrel cactus is a plain green dome, cholla a thin jointed stick, the leaning snag barely leans. |
| the other sheets | | Maple, manzanita (red stems), cattails, ferns (broad fronds from a grass grower: crude), stump, alpine mat (a flat cushion), dead shrub and tumbleweed (bare twig balls) are all told apart. |

## Frame time, classic against biome plants (ms, offscreen AMD, tests running alongside: indicative)

| Spot | drive height: classic / new | wide: classic / new |
|---|---|---|
| temperate forest | 37.2 / 39.7 (+7%) | 53.9 / 49.1 (-9%) |
| hot desert spot | 44.5 / 48.4 (+9%) | 55.8 / 48.4 (-13%) |
| subalpine | 31.7 / 34.1 (+8%) | 50.4 / 48.5 (-4%) |
| riparian / dense montane | 47.3 / 59.3 (+25%) | 56.8 / 55.2 (-3%) |
| canyon rocks | 40.3 / 40.7 (+1%) | 60.9 / 59.7 (-2%) |
| montane | 45.7 / 46.0 (+1%) | 61.1 / 57.9 (-5%) |
| prairie | 30.2 / 38.5 (+27%) | 47.1 / 38.0 (-19%) |
| Moab juniper bench | 60.2 (classic palette, earlier) / 59.3 | |
| Moab river | 89.2 (classic palette, earlier) / 102.0 | |

The 5% target is met in wide views and in four of seven drive-height views, not in the densest wood (6,500 trees in view:
the near trees' full models and their shadows) nor where the camera stands in a crown.

## Since this review (not yet re-shot)

The shots and verdicts above are from before these changes; they have not been taken again.

- The biome areas are marked by the maps' builders now, not a noise (`docs/procedural-plants.md`, "Where a biome is"). On the big
  map: Badlands Wash, the Frontier's badlands and the Open Country's southern canyons are high desert; Ranger Flats and the Open
  Country's meadows are prairie; the Frontier's own west and the dune bowl are hot desert. The spot called `dry_prairie` above
  (-1842,2460) is Timber Basin: mountain forest now. Spots to shoot: prairie -2000,0 and -4300,0; canyon 1500,-4400 and
  -4000,-2400 and 2000,-2000; hot desert the Frontier region's west (1050,-3650) and a desert level (wilds); in the procedural
  world, seed 1: mesa country at global 1072,2717, dunes 1722,2342, plains 706,3217.
- A classic tree is planted as its species: it is that species' height in the world and its `Pole` is that stem. The unseen
  post over a short desert or treeline tree is gone, and each species has its own trunk.

## Still wrong
- The layout is unchanged, so a biome has trees only where the map planted some: the Frontier's desert has a few snags, the dune
  bowl a rim of trees, the procedural dunes almost none (their desert is shrubs and cacti). Prairie has a forest's tree count
  on the Open Country's meadows.
- With `--classic-plants` the classic models are drawn on the species' trees: a classic trunk thicker or thinner than its `Pole`.
- The procedural world's far forest (the backdrop past the window) still draws classic heights: taller than the short desert
  and treeline trees the window plants.
- Leaf-card shadows are still whole cards: cutting them cost 2 ms a frame in a forest (the shader is there, unused).
- Fallen logs are the game's own classic logs; the generator has stumps and leaning snags but no lying trunk.
