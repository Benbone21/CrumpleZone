# Procedural plants: review (branch wip/proc-plants)

The generator and how to add a species: `docs/procedural-plants.md`. All shots 10:00, `--photo --adaptive 0`, 1600x900,
offscreen, frame 240. Judged against how the plants look from memory of them and of photos; nothing was downloaded.

## Model sheets (`build/modelsheet tree_gen shrub_gen`)

The sheet's renderer draws leaf cards whole; the game cuts them to a leafy outline, so crowns are more ragged in the game.

| File | What |
|---|---|
| `sheet-trees-1-seeds.jpg`, `sheet-trees-2-seeds.jpg` | Every tree species, seeds 1 (two views), 2 and 3 |
| `sheet-trees-1-lods.jpg`, `sheet-trees-2-lods.jpg` | Seed 2 full, then its near, far and farthest copies |
| `sheet-shrubs-seeds.jpg`, `sheet-shrubs-lods.jpg` | The same for the shrubs (two far copies) |

| Species | Verdict |
|---|---|
| Fir | Reads as a fir: tiers of flat boughs drooping low down, a spire, trunk showing between tiers. Boughs are a little sparse close up. |
| Spruce | Narrower and darker than the fir, short hanging boughs. Told apart from the fir in a stand. |
| Ponderosa | The best of the conifers: clean orange trunk, open crown of upturned limbs with tufts. Needle tufts are lumps, not needles. |
| Pinyon | Small, dense, rounded, one crooked trunk. Plausible; could be mistaken for a small broadleaf. |
| Utah juniper | Thick twisted grey trunk forking low, limbs showing, olive foliage, a bleached dead spike on some. Some seeds grow a full rounded crown, others an open V of two or three limbs that is too thin for a juniper. |
| Fremont cottonwood | Pale forked trunk and big limbs show under a broad open crown; bright yellow-green. Trunk is short and stout for a mature tree; no shimmer. |
| Aspen | White trunk bare for half its height, narrow crown of small light leaves. Recognisable. |
| Oak | Stout forked trunk, wide dark crown. Generic but an oak. |
| Tamarisk | Arching red-brown stems with feathery grey-green plumes, a little pink. Thinner than a real thicket. |
| Snags (grey, burnt, broken) | Bare trunks with snapped limbs: the grey one is good; the broken one is a blunt post. |
| Sagebrush | Gnarled stems under grey-green tufts. Tufts are too coarse: it reads as a small tree more than a fine twiggy shrub. |
| Blackbrush | Low dark mound with grey twigs. Fine at a distance. |
| Mormon tea | A broom of jointed yellow-green stems forking upward: no longer grass. |
| Rabbitbrush | Erect grey-green stems with yellow heads. Good near; sparse in its far copy. |
| Saltbush | Pale rounded mound. Fine. |
| Yucca | Rosette of narrow swords, dead skirt, a stalk of cream bells on some. Good. |
| Prickly pear | A low clump of small pads (about 10-15 cm on a 0.5 m clump) with fruit. Pads no longer oversized; the clump is a little loose. |
| Bunchgrass | Pale arching tuft with seed stalks. Good. |
| Coyote willow | Thin upright wands with narrow leaves. Reads as canes; a real thicket is denser. |

## In the game

| Shot | Camera | Verdict |
|---|---|---|
| `moab-60-juniper-bench.jpg` (before: `-classic`) | Moab 6300,800,3,0,-4 | Junipers are rounded olive crowns on visible trunks instead of stacked dark blobs; shrubs are told apart (mounds, brooms, tufts). The crowns are more open than the classic lumps, and at 60-100 m a few look thin. |
| `moab-63-juniper-close.jpg` (before: `-classic`) | Moab 6318,812,1.8,35,-2 | Trunk, fork, limbs and a dead spike all read at 25 m, which the classic model's did not. This individual's crown is two limbs' worth: too open. |
| `moab-64-drive-height.jpg` | Moab 6323,808.5,1.5,22,5 | From a driver's eye at 12 m: a thick twisted shreddy trunk forking at the ground, limbs through the foliage. The foliage is faceted tufts and cut cards: a stylised tree, not a portrait; foliage still darker than the dusty yellow-olive of the real thing. |
| `moab-61-river-corridor.jpg` (before: `-classic`) | Moab 4300,4420,14,-90,-8 | Cottonwoods have pale forked trunks under ragged bright crowns; willows are wands, tamarisk feathery. The understory is thinner-looking than before (wands against lollipops), and the bank is still bare sand between plants. |
| `forest-backcountry-classic.jpg` / `-generated.jpg` | Backcountry -80,-250,2,-39,-2, without and with `--proc-plants` | Generated: a mixed conifer forest with tiered firs, an orange-trunked ponderosa, a white aspen; trunks everywhere. It is darker and less saturated than the classic cones and lollipops, and a different art style from the rest of that map. The owner's call. |
| `forest-frontier-classic.jpg` / `-generated.jpg` | Frontier 0,0,2.5,30,-2, without and with `--proc-plants` | The same choice in open woodland: oaks with real limbs against green lollipops. |

## Numbers (offscreen, AMD GPU, other jobs running: indicative only)

| Scene | Before | After |
|---|---|---|
| Moab juniper bench (60): frame, tree / bush triangles | 60.2 ms, 2k / 3k | 56-61 ms, 10k / 4k |
| Moab juniper close (63) | 54.0 ms, 3k / 3k | 52-56 ms, 13k / 5k |
| Moab river corridor (61) | 89.2 ms, 19k / 19k | 97-103 ms, 101k / 14k |
| Backcountry forest, `--proc-plants` off / on | 33.5-35.1 ms, 43k trees | 40.8-42.1 ms, 196k trees |
| Frontier, `--proc-plants` off / on | 39.3 ms, 33k trees | 41.6 ms, 144k trees |
| Moab peak memory (RSS) | 3.43-3.47 GB | 3.44-3.50 GB |
| Moab run, start to frame 240 (wall) | 37-45 s | 37-50 s (same per-frame share) |

On the river the per-stage timers put the difference in the terrain's GPU time (69 to 81 ms), not the vegetation's (5.3 to 4.2 ms):
the frame costs the same with every tree forced to its farthest copy. The likeliest cause is more ground shaded through the open
crowns than behind the classic solid lumps; it was not chased further.

## Still wrong

- Foliage is geometry: lumpy tufts and cards cut by a noise. Close up it is plainly low-polygon. Leaf textures would be the next step.
- Juniper crowns vary too much: about a third of the seeds are too open.
- Sagebrush is too coarse; willow and tamarisk thickets too thin; rabbitbrush's far copy is a few stalks.
- No wind: nothing in the game sways, so nothing was added.
- The leaf cards' shadows are whole cards (the shadow pass doesn't cut them).
- Species files are not part of a map's hash (the palette is), yet a tree species' stem sets its `Pole`'s radius: two peers with different species files would have slightly different trunk colliders on a `plants procedural` map. The species text should be hashed with the palette.
