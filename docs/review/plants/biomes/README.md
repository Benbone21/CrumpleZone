# Biome vegetation: review (branch wip/plant-biomes)

Seven palettes in `assets/maps/vegetation/`, 44 species in `assets/plants/`. How it works: `docs/procedural-plants.md`, "Biomes".
Every image here was taken on the build of 2026-11-20 (the far tufts, the shrub and cactus fixes and the fallen log are in).
Shots: 10:00, `--photo --adaptive 0`, 1600x900, offscreen, frame 240. Each biome image is four views of one spot: top a wide view
from 24 m up, bottom from a driver's eye (2 m); left `--classic-plants`, right the default (generated, by biome). The sheets
(`sheet-PALETTE.jpg`) show every species a palette's `stand` lines name, seed 2 (leaf cards uncut, as the sheet tool draws them).
Judged against how the plants and places look from memory of them and of photos; nothing was downloaded.

## In the game

| File | Map, eye (x,z), yaw | Verdict |
|---|---|---|
| `temperate_forest.jpg` | Backcountry -54,-250, 240 | Firs in level tiers, oaks and maples with forked trunks, an orange-trunked ponderosa, a snag: a mixed wood where there were cones and one lollipop. The firs past 11 m are stacks of flat tufts: tiered, but coarser than the classic cones and plainly low-polygon at 15 m; from above the stacks lean and wobble. Broadleaf crowns are faceted lumps at any distance. |
| `montane_forest.jpg` | Backcountry -2112,1088, 240 | Ponderosas with clean orange trunks over a grassy floor, firs, a leaning snag, a stump: reads as an open ponderosa stand, which is right for the biome. The near fir (full model) is a thin spray of dark cards on a bare pole, sparser than a real fir; the ground shows under every crown where the classic cones hid it (the frame-time cost below). |
| `riparian_wetland.jpg` | Backcountry -2061,1296, 0 | The river bank: cottonwood and maple crowns over white aspen stems and grey trunks, leaning dead trees at the edge. From 30 m the crowns are big square-ish tufts and the stand is one mass; the stems under it are the improvement. No willow or cattail shows at this distance: the bank itself is bare gravel on both sides. |
| `subalpine.jpg` | Backcountry 1856,-3136, 0 | Snow-laden stunted firs and spruce, broken and leaning snags: reads as treeline, and the trees are shorter than the classic ones as they should be. Under snow the far tufts are white blobs and the species can't be told apart; the classic cones were crisper. |
| `dry_prairie.jpg` | Backcountry -3904,1088, 120 | Juniper, ponderosa, cottonwood and oak over grass, a grey snag, yucca rosettes. The trees are right for a prairie's draws, but there are a forest's worth of them: the layout is the map's, so this is parkland, not prairie. |
| `dry_prairie-procedural.jpg` | Procedural seed 1, global 706,3217, 120 | The procedural plains: scattered cottonwoods and oaks by a river, dry grass tufts on brown ground. Closer to a prairie than the hand-made map; the grass tufts are few and small against the ground texture. |
| `colorado_plateau_desert-moab.jpg` | Moab 6300,800 (bench) and 4300,4420 (river); generated only | The one map made for the high desert (its palette is procedural either way, so there is no classic half). Bench: scattered dark junipers and low shrubs on red ground, right in density and colour. River: cottonwoods on pale forked trunks with shrubs between; their crowns from 30 m are a dozen large flat tufts each, lumpier than a cottonwood's fine canopy. |
| `colorado_plateau_desert-backcountry.jpg` | Backcountry 1216,-2112, 0 | Slickrock with a dead shrub, a yucca and dry grass where the classic look had a yucca on a trunk and a tumbleweed star. Sparse and dry-coloured as it should be; from 24 m up the plants are specks (the classic green cacti showed more, wrongly). No trees in this view. |
| `colorado_plateau_desert-procedural.jpg` | Procedural seed 1, global 1072,2717, 0 | Mesa country: one juniper (olive, squat, right) and a snag where the classic look put a green lollipop and saguaro-shaped cacti. Very sparse: the world plants little here. |
| `hot_desert-backcountry.jpg` | Backcountry 320,-192, 120 | Not a desert: the dune bowl's rim is grass. A saguaro column, a palo verde and a broken snag stand on a lawn beside sand. Shows the species in the game's light (the saguaro's ribs read; the palo verde is a few green sticks with tufts), not the biome. |
| `hot_desert-level.jpg` | The DUNES level, 448,-64, 0 | The desert level's sand: two or three shrubs in view (a dead shrub, a dry tuft; the far cacti are dark specks where the classic ones were bright green). There is no ground in the game where the hot desert palette grows thickly enough to judge as a place: see its sheet. |

No map has riparian plants in any number away from Backcountry's rivers and the Frontier's bayou (the Frontier spot 320,-448 was tried:
a grassy hill with no water in view). The Frontier's desert has one living tree.

## Sheets

| File | Verdict |
|---|---|
| `sheet-temperate_forest.jpg` | Fir, spruce, ponderosa, oak, maple and aspen are told apart by habit and bark. The fern is fronds of paired leaflets now and reads as a fern. The fallen log is a short thick trunk with stubs. The stump (seed 2) ends in a cone: the snapped cap's height is random and a tall one makes a hut of a stump. |
| `sheet-montane_forest.jpg` | Ponderosa is the best tree. The burnt snag is a black pole with stubs: fine. Manzanita shows its red stems. Sagebrush has smaller tufts than it had but is still an open twiggy bush, not a dense silver mound. |
| `sheet-riparian_wetland.jpg` | Coyote willow is a denser thicket of canes in two clumps (20 to 24 stems for 13 to 17); still canes with leaves, thinner than a real thicket. Cattails are good. |
| `sheet-subalpine.jpg` | Krummholz is a lopsided stunted fir: right. The alpine mat is a flat green cushion: plausible, plain. |
| `sheet-dry_prairie.jpg` | Juniper's crown is fuller (foliage from further down the limbs) but this seed still forks into two masses: an open V, less bare than before. Rabbitbrush, yucca and bunchgrass are good. |
| `sheet-hot_desert.jpg` | Saguaro has ribs and upturned arms. The barrel cactus is a ribbed dome with a ring of yellow flowers, taller than most real ones. Cholla is a clump of stubby pale joints on a dark trunk; the joints end in points, not blunt ends. Ocotillo with red tips is good. Mesquite and catclaw are much alike. The leaning snag does lean (27 to 45 degrees: 6 m sideways on the sheet's size line); the sheet's two views look along the lean for this seed. |
| `sheet-colorado_plateau_desert.jpg` | Moab's species (with tamarisk and coyote willow, which its zones name). Tamarisk has more plumes (5 cards a clump, closer together); pinyon could still be taken for a small broadleaf. |

## Frame time, classic against biome plants

ms a frame, offscreen on the integrated AMD GPU, one pass, tests and another agent's jobs running alongside: indicative only. A
difference of a few percent is noise here; the per-stage GPU timers below repeat to 0.1 ms.

| Spot | drive height: classic / new | wide (24 m up): classic / new |
|---|---|---|
| Backcountry temperate forest (-54,-250) | 40.6 / 40.1 (-1%) | 57.3 / 53.3 (-7%) |
| Backcountry dense riverside wood (-2061,1290) | 79.7 / 57.4 (-28%) | 67.1 / 65.9 (-2%) |
| Backcountry the same bank, clear view (-2061,1296) | 74.0 / 67.5 (-9%) | 65.1 / 64.8 (0%) |
| Backcountry montane (-2112,1088) | 45.5 / 53.2 (+17%) | 65.3 / 64.1 (-2%) |
| Backcountry Timber Basin (-1842,2460; the last review's "prairie") | 37.4 / 45.7 (+22%) | 54.0 / 49.0 (-9%) |
| Backcountry prairie (-3904,1088) | 40.5 / 39.7 (-2%) | 64.5 / 56.6 (-12%) |
| Backcountry subalpine (1856,-3136) | 40.8 / 40.6 (0%) | 60.7 / 60.6 (0%) |
| Backcountry canyon (1216,-2112) | 41.4 / 40.9 (-1%) | 62.0 / 61.9 (0%) |
| Backcountry canyon (960,-1088) | 44.8 / 45.9 (+3%) | 86.6 / 83.9 (-3%) |
| Backcountry dune bowl (320,-192) | 45.7 / 47.9 (+5%) | 60.4 / 57.7 (-4%) |
| Frontier (-192,192) | 43.4 / 45.4 (+5%) | 65.1 / 64.5 (-1%) |
| Frontier open wood (320,-448) | 35.3 / 42.1 (+19%) | 56.9 / 55.1 (-3%) |
| Procedural plains (seed 1, 706,3217) | 69.6 / 69.0 (-1%) | 69.8 / 69.5 (0%) |
| Procedural mesa country (seed 1, 1072,2717) | 42.4 / 42.4 (0%) | 59.9 / 60.3 (+1%) |
| DUNES level (448,-64) | 54.3 / 53.6 (-1%) | 44.7 / 44.9 (0%) |
| Moab juniper bench (generated only) | 60.6 (59.3 to 60.0 before this pass) | |
| Moab river (generated only) | 104.6 (102.0 to 103.2 before) | |

Moab peak memory: 3.53 GB RSS (3.51 before; under the 4 GB limit).

Within 5 % at drive height everywhere but three spots, all open conifer stands on grass. What the difference is made of, by the
renderer's per-stage GPU timers at Timber Basin (`--bench 150,90`; classic 40.8 ms a frame, generated 49.3 before this pass and
48.9 now):

| Stage (GPU ms) | classic | generated, before | generated, now | What it is |
|---|---|---|---|---|
| Terrain | 18.0 | 21.9 | 21.8 | The ground is drawn after the trees. A classic fir is a solid cone to the ground; a ponderosa is a crown on a bare trunk and a fir is open tiers, so more ground is shaded (with every tree forced to its plainest copy: the same 22 ms; with no trees at all: 35.8 ms). This is the look, not a level of detail: it stays. |
| Vegetation | 8.3 | 10.8 | 10.8 | The near trees' full and near copies (1.2 ms) and the far copies between 30 and 100 m (1.0 ms); with every tree at its plainest copy it is the classic's 8.6 ms. |
| Shadows | 3.0 | 4.8 | 4.1 | The trees' share is 2.0 against 3.8 ms: what the far copies' tufts cover in the shadow map (not their triangles: forcing the plainest copy changed nothing; smaller tufts did). Tufts grown 2.2 and 3.3 times in place of 2.7 and 4.4: 4.1 ms. |

In the dense riverside wood the generated plants were already the cheaper (classic: vegetation 34.4 ms GPU, a wall of solid
crowns; generated 22.0). There the near copy's reach was the cost: the trees between 28 and 36 m in their 2,000-triangle copy took
4.7 ms a frame (measured with the reach at 28 m), so it now ends at 30 m: vegetation 18.8 ms, the frame 63.8 against the classic's
82.3 in that run.

The leaf cards' shadow cut-out (`kTreeDepthFS`) stays off: it took the shadow pass from 4.1 to 10.2 ms at Timber Basin and from 11.5
to 19.9 ms in the dense wood (+6.1 and +8.4 ms a frame), and a generated tree's shadow is drawn from its far copies, which have no
cards to cut: only shrubs within 8 m would show it.

## Still wrong

- Open conifer stands cost 17 to 22 % more at drive height than the classic cones (above): 4 ms of it is ground the classic trees
  hid. Bringing it down means cheaper ground shading or more low cover, not plainer plants.
- Far copies are fewer, bigger tufts: the right size now and shaded as one crown, but still lumps; a fir past 11 m is a stack of
  flat tufts. Leaf textures or baked pictures nearer than 200 m would be the next step.
- The hot desert has no ground of its own with plants on it; prairie has a forest's tree count on the hand-made map. The layout
  is unchanged: only what grows changed.
- A leaning snag leans up to 45 degrees but its `Pole` stands upright under it: the top of the trunk is not where the collider is.
- The fallen log and the stump stand in for classic bushes: bush-sized (a log one to two and a half metres long, a stump up to a
  metre across) and nothing to hit. The game's own classic logs are the ones a car hits.
- Dead limbs end in long points (thick spikes close up); a stump's snapped cap can be a cone.
- Juniper still forks into an open V on some seeds (its skeleton was left alone so the trunks' colliders did not change);
  sagebrush is still too open; cholla joints are pointed.
- With `--classic-plants` the classic models are drawn on the species' trees: a classic trunk thicker or thinner than its `Pole`.
- The procedural world's far forest (the backdrop past the window) still draws classic heights.
- Leaf-card shadows are whole cards (cutting them costs 6 to 8 ms a frame, above).
