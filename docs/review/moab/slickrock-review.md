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
| 53 The Staircase | 4291.3,1838.6,4,59.3,11.6 | no photo of the Staircase itself found; the Hell's Revenge slickrock photo above for colour and texture | The four even slab stairs are gone: it is now a narrow pass between two domes with steep banded walls. From this camera the ledges don't show at all, so it no longer reads as a staircase; that may be too subtle for an obstacle named for its steps. |
| 54 The Waterfall (Gold Bar Rim) | -5988.3,4164.5,3,116.9,-24.7 | [roger_up_a_ledge](https://traildamage.com/wp-content/uploads/trails/gold_bar_rim/060323/roger_up_a_ledge.jpg) (a Gold Bar Rim ledge, not the Waterfall) | Better colour and real ground, but the obstacle itself is now nearly invisible from here: the photo's thick ledges with dark undercuts aren't in it. The track beside it is a dark brown strip. |
| 55 Golden Spike (Double Whammy) | -5005.7,4671.7,3,-22.3,2.4 | [aaron_on_the_golden_stairs](https://traildamage.com/wp-content/uploads/trails/golden_spike/040515/aaron_on_the_golden_stairs.jpg) | The ground is the real rim and reads as pale slickrock, but the photo's thick, stacked tabular slabs are missing: the ledges here are too small to see. |

Overall: the "stupid looking rocks" are gone and the land near the obstacles is now the survey's, which is the biggest single gain.
What's missing now is the opposite of before: the ledge-and-slab obstacles (Waterfall, Golden Spike, the Staircase) are too subtle,
since 1 m lidar rounds their steps away and the authored slabs were made small.
