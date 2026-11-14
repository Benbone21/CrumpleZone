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

## Still wrong
- Hot desert, canyon and prairie have no ground of their own on the hand-made maps beyond small patches; the palettes are there, the places are not. The procedural world's biomes use the same ground rules (sand, red rock, snow, water) but no shot was taken there.
- Short stand-ins (mesquite, palo verde, catclaw, saguaro, krummholz) keep the tall classic tree's collider: an unseen post inside or above a low crown.
- One trunk thickness per classic kind: an aspen-grove tree's stand-in has the oak's thicker trunk on the aspen's thinner collider.
- Regions of montane, temperate and prairie on the big maps come from a noise, not from the map's own design.
- Leaf-card shadows are still whole cards: cutting them cost 2 ms a frame in a forest (the shader is there, unused).
- Fallen logs are the game's own classic logs; the generator has stumps and leaning snags but no lying trunk.
