# Windows RTX 4070 Laptop phase 2

Machine as in `windows-rtx4070-baseline.md` (Ryzen AI 9 HX 370, RTX 4070 Laptop, 2048x1280 at 240 Hz, g++ 16.1.0). Windows power plan still **Balanced** (the agent cannot change it).

## Step 0: new baseline (main at dc7fe7f, 1 run each)
- **Tests:** 864 scenarios, 851 passed, 13 failed, 538 s. The same 13 as phase 1: net/bandwidth, tires/shear, tires/debead, structure/rock-hits/4runner, offroad/whoops, components/axle-landing, components/tire-into-fender/rock buggy, components/tire-into-fender/turbo utv, structure/weight/koenigsegg jesko, structure/weight/toyota tacoma, engine/performance, freeway/top-speed/turbo utv, frontier/current. Log: `raw/windows-rtx4070-phase2-step0-run_tests.log`.
- **Physics fingerprint:** `3db86794fec05596`, unchanged from phase 1.
- **Raw bench files:** `raw/suite-20261017-223557-1600x900.txt`, `raw/suite-20261017-223641-2048x1280.txt`.

Average fps, phase 1 to now:

| Map / preset | 1600x900 before | 1600x900 now | 2048x1280 before | 2048x1280 now |
|---|---|---|---|---|
| Frontier POTATO | 504 | 512 | 516 | 536 |
| Frontier LOW | 315 | 315 | 199 | 198 |
| Frontier MEDIUM | 205 | 204 | 130 | 130 |
| Frontier HIGH | 163 | 163 | 107 | 106 |
| Open Country POTATO | 556 | 597 | 619 | 625 |
| Open Country LOW | 366 | 408 | 259 | 262 |
| Open Country MEDIUM | 235 | 243 | 157 | 157 |
| Open Country HIGH | 187 | 187 | 124 | 125 |
| Boulder Gully POTATO | 439 | 440 | 480 | 497 |
| Boulder Gully LOW | 306 | 316 | 225 | 225 |
| Boulder Gully MEDIUM | 216 | 217 | 139 | 140 |
| Boulder Gully HIGH | 167 | 167 | 110 | 110 |

Main's new work (car drawing, physics threading, map loading) changes almost nothing here: the benchmark is GPU-bound and its SIM stage is 0. Only some POTATO and LOW runs moved.

## Items
Not started. Next in the plan: item 1, `net/bandwidth` on Windows.
