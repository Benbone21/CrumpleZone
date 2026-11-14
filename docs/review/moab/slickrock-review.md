# Moab slickrock: before and after (branch wip/moab-slickrock)

Each pair is the same camera (`--eye X,Z,H,YAW,PITCH`, 10:00, `--photo --adaptive 0`, 1600x900, offscreen), "before" built
from wip/moab-trails (9e420ec) with its own map files, "after" from this branch. References are photos viewed on the web, not copied.

These after shots are the first with the lidar really in the land. The earlier rounds' shots and verdicts on this branch were not:
the sidecar key was `detail`, which the parser already used for the fine-bump amplitude, so the patches were never loaded (and the
bumps were turned off). It is `lidar` now, and the patches keep their NoData (they were being filled from neighbours on loading).
On lidar an obstacle's walls, fins, pitch and tilt are the survey's; where rock rises beside a hard trail over it, only about 4.4 m of
tread is levelled, eased into the survey over 3 m (a crest or a fin's top keeps the full width: the first try at narrowing everywhere
dropped the test Bronco off Hell's Revenge's start fin); the authored ledges are uneven in rise and spacing, shorter than the tread
and off-centre, dying into the slope. Also new: sand meets rock along a ragged, feathered edge with grain and ripples; bushes from
the NAIP brush class at about three times the density; and the rock bands sit 130 m higher south-west of the Moab fault zone.

| Pair | Camera | Compared against | Verdict |
|---|---|---|---|
| 50 Hell's Revenge domes, wide | 4900,300,45,-42,-6 | NAIP 2024 0.6 m over the fins (data/real/moab/source/hires/naip2024, block e627600 n4270800); [derek_up_a_steep_hill](https://traildamage.com/wp-content/uploads/trails/hells_revenge/031024/derek_up_a_steep_hill.jpg) | Much better: the skyline is the real fins and knobs, banded and varnished, over bare pinkish rock with sand in the hollows. Still wrong: the foreground plain is smoother than the real Sand Flats, brush is sparse from this height, the trees are green cones, and the sand patches are still flat in colour. |
| 51 Hell's Gate | 4264.7,4034.7,4,41.2,0.6 | [hells_gate.jpg](https://traildamage.com/wp-content/uploads/trails/hells_revenge/060324/hells_gate.jpg), [mike_up_hells_gate_part_1](https://traildamage.com/wp-content/uploads/trails/hells_revenge/060324/mike_up_hells_gate_part_1.jpg) | Close to the photos in form for the first time: a deep V slot between two tall smooth rock walls, the trail a narrow floor at its bottom, the walls banded with varnish streaks. Off: the walls' cross-bedding is too coarse and blotchy (the cliff texture); long straight shading creases cross the foreground (the terrain mesh's big simplified triangles, an older renderer artefact also in the original shots, not the lidar: they stay with the sharpening off); the slot's floor has a bright orange sand pool where the photo has bare rock with a little sand; the ledges at its head don't read at this distance. |
| 52 Escalator | 3896.7,2281.2,4,116.4,-1.5 | [escalator.jpg](https://traildamage.com/wp-content/uploads/trails/hells_revenge/030510/escalator.jpg), [kendall_and_escalator_part_2](https://traildamage.com/wp-content/uploads/trails/hells_revenge/030510/kendall_and_escalator_part_2.jpg) | Much better: a smooth climb up the saddle between two rounded domes, the steps barely visible, which is how the photos read. The domes are paler and less salmon than the photos and their surfaces too uniformly cracked. |
| 53 The Staircase | 4291.3,1838.6,4,59.3,11.6 | no photo of the Staircase itself found; the Golden Spike stairs photo below for the form of tabular ledges | The slab steps read across the floor of the pass, and nothing stands on its walls now (the dark wedges were the beds' last blocks climbing the right wall; beds now stop where the land is steeper than ~19 degrees across or stands well off the step). One pale end block remains at the right of the steps. Still small at this distance. |
| 54 The Waterfall (Gold Bar Rim) | -5988.3,4164.5,3,116.9,-24.7 | [roger_up_a_ledge](https://traildamage.com/wp-content/uploads/trails/gold_bar_rim/060323/roger_up_a_ledge.jpg) (a Gold Bar Rim ledge, not the Waterfall) | Better: shorter, broken runs with gaps, rounded lips and ends, varied thickness, lighter faces with a dark line under the lip. Off: the blocks' tops are still smooth and even, so they read a little like pillows, and the near bed's long top face shows a faint crease where its facets meet. |
| 55 Golden Spike (Double Whammy) | -5005.7,4671.7,3,-22.3,2.4 | [aaron_on_the_golden_stairs](https://traildamage.com/wp-content/uploads/trails/golden_spike/040515/aaron_on_the_golden_stairs.jpg) | Closer to the photo: rounded, broken tabular blocks stepping across the slope, the faces lighter (nearer the photo's pale stone) with the dark undercut line kept. Off: they're a little too uniformly rounded (more boulder-like than the photo's sharp-bedded slabs) and still a touch darker and redder than the photo's cream. |

Overall: the "stupid looking rocks" are gone and the land near the obstacles is now the survey's, which is the biggest single gain.
Hell's Gate and the Escalator read like their photos in form; the ledge obstacles (Staircase, Waterfall, Golden Spike) now have
visible stacked tabular beds (`slabs`), now rounded, broken into uneven runs and lightly varnished rather than crates. The long straight
shading creases on steep terrain (Hell's Gate's foreground) are still there; not looked into this round.

## Cliffs round (branch wip/moab-cliffs)

Same cameras, 10:00, `--photo --adaptive 0`, 1600x900, offscreen, 240 frames (the cliff walls build in the background). `NN-*-cliffs.jpg`
is this branch; compare with `NN-*-after.jpg` above (the slabs-polish build, b1619c1, which reproduces them). 56 and 57 are new: tall
walls near trails, "before" from b1619c1. References are Wingate and Navajo walls as they look in photos: vertical, horizontally
bedded, black varnish curtains from the rim, arched alcoves, hard ledges over shaded recesses.

What changed: (1) on steep faces the ground's patterns are worked out on the face from its two sides, not from above, and with strata
the faces get level bedding (partings, bed tones, shaded lips), varnish curtains and cross-bedding drawn across the face; (2) the
ground is meshed at 1 m with LODs picked by each chunk's error, and the screen-space occlusion ignores folds under ~12 degrees, which
is what the "long straight creases" were (the occlusion of the 2 m mesh's folds, not shadows or paint); (3) carved cliff walls drawn
over the land's faces (undercut soft beds, a set-back cap under the rim, alcoves), out of every tire's reach; (4) scanned cliff pieces
along rims and wall feet, off the trails.

| Shot | Camera | Verdict |
|---|---|---|
| 50 domes wide | 4900,300,45,-42,-6 | Better at distance: the far cliffs read as walls with dark vertical varnish curtains and level bands instead of soft blotchy ramps. The plain in front is unchanged (as smooth as before). |
| 51 Hell's Gate | 4264.7,4034.7,4,41.2,0.6 | The straight dark creases across the foreground are gone. The left wall's near face now reads as broken, bedded rock. The slot's walls are slickrock domes, not cliffs, so the carved walls barely touch it; the sand pool at the bottom is still too bright and too orange. |
| 52 Escalator | 3896.7,2281.2,4,116.4,-1.5 | Little change; a few dark cliff pieces now sit on the distant rim at the right. No regressions. |
| 53 The Staircase | 4291.3,1838.6,4,59.3,11.6 | Much better: the left wall had big triangular facets and creases; now it's a continuous bedded, varnished face with level partings. The upper slope still has a few chevron-shaped facets. |
| 54 The Waterfall | -5988.3,4164.5,3,116.9,-24.7 | Small change: the ridge behind gets a broken caprock edge. The slabs are as before. |
| 55 Golden Spike | -5005.7,4671.7,3,-22.3,2.4 | Small change: the rim on the skyline has a few more broken blocks. |
| 56 Potash wall (new) | -2492,-4041,2,-112,15 | The best gain. Before: the wall was one stretched texture with vertical smears and a mesh-grid look. Now it's horizontally bedded with partings, deep shaded undercuts under harder beds, varnish curtains, a lumpy broken rim and an alcove on the left wall, which is close to a Wingate wall in a photo. Off: the scanned pieces on the face read as rounded boulder clusters stuck on the wall (the Namaqualand model is lumpy), the photo texture still visibly repeats every 4 m on the face, and the undercuts are too black and too even in depth. |
| 57 Gold Bar Rim wall (new) | -4968,5130,45,90,22 | Better: the 90 m wall gains level bedding, two big arched alcoves upper left and a broken cap. Off: the face is still ribbed vertically (the 10 m DEM's grid, not lidar here), and the talus below keeps the `ledges` stair pass's waffle grid, which looks artificial. |

Still wrong overall: the scanned cliff pieces look like stuck-on boulders rather than broken caprock; the 10 m DEM walls stay ribbed;
the `ledges` stair pass leaves a criss-cross waffle on talus slopes; and the photo layer repeats on big faces. The carved walls only
show within ~285 m (beyond, the land's face with the shader's bedding).

## Cliffs round 2 (same branch)

Same cameras and settings; `NN-*-cliffs2.jpg`, compared with round 1 (`NN-*-cliffs.jpg`, and `56/57-*-after.jpg`).

| Shot | Verdict |
|---|---|
| 50 domes wide | Same form as round 1; the open sand is a little duller (the sand-pool fix is map-wide), still clearly sand against the rock. |
| 51 Hell's Gate | The sand pool at the slot's floor is no longer a glowing orange: duller and closer to the photo's dusty sand over rock. The walls are as in round 1. |
| 52-55 | No visible change beyond the sand tone. |
| 56 Potash wall | Better: the boulder clusters stuck on the face are gone; a few angular caprock blocks sit at the rim; the undercuts are brown shaded recesses that come and go, not black slots; the photo's 4 m repeat is broken up. Off: the caprock is sparse at this camera, the undercuts read a little soft, and the bed partings are still very regular. |
| 57 Gold Bar Rim wall | Much better: the criss-cross waffle on the talus is gone (it was the two side projections crossing on a 35-55 degree slope, not the ledges pass), and the 10 m data's vertical ribs are cut back to a smoother bedded face. Off: some ribbing remains at the right where the face turns, and the alternating orange and brown bands are bolder than Wingate in photos. |

Also: the walls' depth no longer pulls in front of where a vehicle could be (only to where the line of sight leaves the rock,
plus 10 cm and 0.3% of the distance). Still wrong: the bed partings are too regular and evenly spaced, there are few alcoves at
these two cameras, the talus under Gold Bar is plain where real talus is blocky, and beyond ~285 m the walls are the land's face
with the shader's bedding only.

## Vegetation round (branch wip/moab-vegetation)

The pines were the importer's own: with `climate desert` it planted the mountain maps' conifer model (75% firs 3-6.5 m, 25% snags)
on any ground that wasn't rock, by a noise, and the desert habitat's bushes (saguaros among them) on the photos' brush. Moab now
names a vegetation palette (`vegetation colorado_plateau_desert`, `assets/maps/vegetation/`, docs/real-terrain.md "Vegetation
palettes"): zones by ground cover, slope, real elevation and distance to water, each with its species. 39,836 trees (all pines and
snags) became 18,626 (10,410 juniper, 1,779 pinyon, 693 grey snags, 2,081 cottonwood, 3,663 tamarisk); 370 k bushes became 412 k of
twelve dry-country kinds (no saguaros). Same settings as above (10:00, `--photo --adaptive 0`, 1600x900, offscreen, frame 240);
"before" is the same build with the sidecar's `vegetation` line removed (which reproduces main's land hash). Judged against how the
place looks from memory of it and of photos, not against a photo side by side: nothing was downloaded this round.

| Shot | Camera | Verdict |
|---|---|---|
| 50 domes wide (`-veg`) | 4900,300,45,-42,-6 | The green cones and the saguaro are gone; a few dark juniper clumps stand where the photos show brush. Honest: it is now nearly bare, barer than the real Sand Flats, whose blackbrush and scattered junipers show from this height. Shrubs stop being drawn at 130-250 m and the cover's brush class is thin here. |
| 60 juniper bench | 6300,800,3,0,-4 | Better and plausible: low, rounded, dark olive junipers 2-5 m tall spaced tens of metres apart over sand and slickrock, a pinyon (right), blackbrush and yucca between, nothing on the bare rock. Off: the crowns are chunky low-polygon lumps with no visible twisted trunk or dead grey limbs at this distance, and the green is still a little too clean; real junipers are dustier and more ragged. |
| 61 river corridor | 4300,4420,14,-90,-8 | The biggest change: a cottonwood gallery with willow and tamarisk under it along the Colorado, where there were saguaros and firs on bare sand. Off: the cottonwoods read as generic broad green trees (flat-shaded lumps; no pale furrowed trunk showing, no shimmer), the willows as clusters of pale lollipops, the ground under them is bare red sand where the real bank is a tangle, and the far bank is thinner than the near one. |
| 62 sand flat | 4900,300,2.2,-42,-3 | Right in kind: open sand and slickrock with a sagebrush and a saltbush, junipers in the distance. The older sagebrush model is mint-coloured, not the grey-green of the real thing. |
| 63 juniper close (after only) | 6318,812,1.8,35,-2 | Reads as a juniper in silhouette (wide as tall, foliage to the ground). Up close it is a pile of faceted blobs; the dead limb barely shows. |
| 64 new trees (model sheet) | `build/modelsheet tree_dryland` | Juniper and pinyon are told apart (olive and round against darker and pointed); cottonwood is the broad one; tamarisk a clump of upright grey-green plumes. All are in the game's low-polygon style, none is a portrait. |
| 65 new shrubs (model sheet) | `build/modelsheet bush`, designs N-T | Blackbrush (dark spiny mound), rabbitbrush (yellow heads) and bunchgrass read well. Mormon tea looks more like a grass spray than a jointed broom, the saltbush like a pale boulder, the prickly pear's pads are oversized. |
| 66 map from above | `build/realterrain make --preview` | Before: trees (dark dots) evenly over everything that isn't rock. After: cottonwood (bright green) along the Colorado, in the Matheson wetland bend, up Courthouse Wash and on the valley floor; juniper (dark) on the benches and mesas; the sand and the slickrock empty. |

Still wrong: shrubs aren't drawn far enough for a bench to look brushy from a height; no cryptobiotic soil; cottonwoods on the
valley floor stand where the photos show any dark patch (fields and yards included); the wash zone follows every small drainage,
so sage and rabbitbrush line slickrock runnels that are bare in life; tamarisk is all green (much of Moab's is beetle-browned).
